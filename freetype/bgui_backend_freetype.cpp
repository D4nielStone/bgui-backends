#include <bgui_backend_freetype.hpp>
#include "os/asset_manager.hpp"
#include "utils/logging.hpp"
#include <unordered_map>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <set>
#include <thread>

static std::unordered_map<std::string, std::string> s_system_fonts;
static FT_Library s_ft;
static std::mutex s_ft_mutex;
static std::mutex s_system_fonts_mutex;

namespace {
    struct font_load_request {
        std::string name;
        std::string path;
        unsigned int resolution;

        std::string key() const {
            return bgui::font_manager::make_key(name, resolution);
        }
    };

    struct font_load_result {
        font_load_request request;
        bgui::font value;
        std::exception_ptr error;
    };

    bgui::font build_font_atlas(
        const std::string& font_name,
        const std::string& font_path,
        unsigned int resolution
    );

    class font_loader {
    public:
        void enqueue(font_load_request request) {
            std::lock_guard lock(m_mutex);
            if (m_pending.contains(request.key()))
                return;

            if (!m_worker.joinable()) {
                m_stopping = false;
                m_worker = std::thread([this] { run(); });
            }
            m_pending.insert(request.key());
            m_requests.push_back(std::move(request));
            m_condition.notify_one();
        }

        bool take_completed(font_load_result& result) {
            std::lock_guard lock(m_mutex);
            if (m_results.empty())
                return false;

            result = std::move(m_results.front());
            m_results.pop_front();
            m_pending.erase(result.request.key());
            return true;
        }

        void stop() {
            {
                std::lock_guard lock(m_mutex);
                if (!m_worker.joinable())
                    return;
                m_stopping = true;
            }
            m_condition.notify_all();
            m_worker.join();
            std::lock_guard lock(m_mutex);
            m_stopping = false;
        }

        ~font_loader() {
            stop();
        }

    private:
        void run() {
            for (;;) {
                font_load_request request;
                {
                    std::unique_lock lock(m_mutex);
                    m_condition.wait(lock, [this] {
                        return m_stopping || !m_requests.empty();
                    });
                    if (m_stopping && m_requests.empty())
                        return;
                    request = std::move(m_requests.front());
                    m_requests.pop_front();
                }

                font_load_result result;
                result.request = std::move(request);
                try {
                    std::lock_guard lock(s_ft_mutex);
                    result.value = build_font_atlas(
                        result.request.name,
                        result.request.path,
                        result.request.resolution
                    );
                } catch (...) {
                    result.error = std::current_exception();
                }

                {
                    std::lock_guard lock(m_mutex);
                    m_results.push_back(std::move(result));
                }
            }
        }

        std::mutex m_mutex;
        std::condition_variable m_condition;
        std::deque<font_load_request> m_requests;
        std::deque<font_load_result> m_results;
        std::set<std::string> m_pending;
        std::thread m_worker;
        bool m_stopping = false;
    };

    font_loader& get_font_loader() {
        static font_loader loader;
        return loader;
    }
}

void bgui::set_up_freetype() {
    bgui::detail::log_out() << "[FreeType BackEnd]Setting up FreeType and searching for default fonts.\n";

    if (FT_Init_FreeType(&s_ft)) {
        throw std::runtime_error("Error initializing Freetype.");
    }

    bgui::detail::log_out() << "[FreeType BackEnd] Initialized.\n";

    ft_search_system_fonts();

    std::unordered_map<std::string, std::string> system_fonts;
    {
        std::lock_guard lock(s_system_fonts_mutex);
        system_fonts = s_system_fonts;
    }

    for(auto font : system_fonts) {
        bgui::detail::log_out() << " - " << font.first << ": " << font.second << "\n";
    }

    bgui::detail::log_out() << "[FreeType BackEnd] Total system fonts found: " << system_fonts.size() << "\n";

    std::string default_font_name;
    std::string default_font_path;

    const std::vector<std::string> monospace_families = {
        "cascadia mono", "cascadia code", "consolas", "dejavu sans mono",
        "liberation mono", "source code pro", "fira code", "fira mono",
        "jetbrains mono", "roboto mono", "ubuntu mono", "noto sans mono",
        "droid sans mono", "courier new", "courier", "menlo", "monaco",
        "mono", "fixed"
    };
    const auto lowercase = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };

    auto monospace_font = system_fonts.end();
    for (const auto& family : monospace_families) {
        auto matching_font = system_fonts.end();
        for (auto it = system_fonts.begin(); it != system_fonts.end(); ++it) {
            const auto name = lowercase(it->first);
            if (name.find(family) == std::string::npos)
                continue;

            if (matching_font == system_fonts.end())
                matching_font = it;
            if (name.find("regular") != std::string::npos ||
                name.find("book") != std::string::npos ||
                name.find("normal") != std::string::npos) {
                matching_font = it;
                break;
            }
        }
        if (matching_font != system_fonts.end()) {
            monospace_font = matching_font;
            break;
        }
    }

    if (monospace_font != system_fonts.end()) {
        default_font_name = monospace_font->first;
        default_font_path = monospace_font->second;
        bgui::detail::log_out() << "[FreeType BackEnd] Loading default monospace font: "
                  << default_font_name << "\n";
    } else if (const auto it = system_fonts.find("Arial CE-Bold"); it != system_fonts.end()) {
        default_font_name = it->first;
        default_font_path = it->second;
        bgui::detail::log_out() << "[FreeType BackEnd] Loading default font: " << default_font_name << "\n";
    } else {
        bgui::detail::log_err() << "[FreeType BackEnd] WARNING: Default font not found. Trying another font instead.\n";
        if (system_fonts.empty())
            throw std::runtime_error("No system fonts were found.");
        default_font_name = system_fonts.begin()->first;
        default_font_path = system_fonts.begin()->second;
        bgui::detail::log_out() << "[FreeType BackEnd] Loading fallback default font: " << default_font_name << "\n";
    }

    ft_load_font(default_font_name, default_font_path, bgui::font_manager::m_default_resolution);
}

bgui::font& bgui::ft_load_system_font(const std::string& path) {
    // 1: Verify if it's font was already loaded
    if(font_manager::get_instance().has_font(path) || path == "default")
        return font_manager::get_instance().get_font(path);
    bgui::detail::log_out() << "[FreeType BackEnd] Loading system font: " << path << "\n";

    std::string font_path;
    {
        std::lock_guard lock(s_system_fonts_mutex);
        const auto font = s_system_fonts.find(path);
        if (font != s_system_fonts.end())
            font_path = font->second;
    }
    if (!font_path.empty())
        return ft_load_font(path, font_path, bgui::font_manager::m_default_resolution);
    return font_manager::get_instance().get_font("default");
}

void bgui::shutdown_freetype() {
    auto& loader = get_font_loader();
    loader.stop();

    std::exception_ptr load_error;
    font_load_result result;
    while (loader.take_completed(result)) {
        if (!load_error && result.error)
            load_error = result.error;
    }
    FT_Done_FreeType(s_ft);
    bgui::detail::log_out() << "[FreeType BackEnd] Shutdown.\n";
    if (load_error)
        std::rethrow_exception(load_error);
}

static std::vector<std::string> split_filters(const std::string& filters) {
    std::vector<std::string> result;
    std::string token;

    for (char c : filters) {
        if (c == ',') {
            if (!token.empty())
                result.push_back(token);
            token.clear();
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            token += c;
        }
    }

    if (!token.empty())
        result.push_back(token);

    return result;
}

// Scan system fonts
void bgui::ft_search_system_fonts(const std::string& filter) {
    std::lock_guard ft_lock(s_ft_mutex);
    auto filters = split_filters(filter);

    std::vector<std::filesystem::path> folders;
#ifdef _WIN32
    folders.emplace_back("C:\\Windows\\Fonts");
#else
    folders.emplace_back("/usr/share/fonts");
    folders.emplace_back("/usr/local/share/fonts");
    if (const char* home = std::getenv("HOME")) {
        folders.emplace_back(std::filesystem::path(home) / ".local/share/fonts");
        folders.emplace_back(std::filesystem::path(home) / ".fonts");
    }
#endif

    bgui::detail::log_out() << "[FreeType BackEnd] Scanning fonts in:";
    for (const auto& folder : folders)
        bgui::detail::log_out() << " " << folder.string();
    if (!filters.empty()) {
        bgui::detail::log_out() << " with filters: ";
        for (auto& f : filters) bgui::detail::log_out() << f << " ";
    }
    bgui::detail::log_out() << "\n";

    std::unordered_map<std::string, std::string> discovered_fonts;
    // search recursively for font files
    for (const auto& folder : folders) {
        std::error_code iterator_error;
        if (!std::filesystem::is_directory(folder, iterator_error))
            continue;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 folder,
                 std::filesystem::directory_options::skip_permission_denied,
                 iterator_error)) {
        // first pick the file
        if (!entry.is_regular_file()) continue;
        auto path = entry.path().string();

        // then verify the extension
        const auto extension = entry.path().extension().string();
        std::string lower_extension = extension;
        std::transform(lower_extension.begin(), lower_extension.end(), lower_extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower_extension != ".ttf" && lower_extension != ".otf")
            continue;

        // multiple filters process
        if (!filters.empty()) {
            bool match = false;
            // for each filter, verify in the path name if it contains.
            for (const auto& f : filters) {
                if (path.find(f) != std::string::npos) {
                    match = true;
                    break;
                }
            }
            if (!match) continue;
        }

        // load a ft face to extract metadatas.
        FT_Face face;
        if (!FT_New_Face(s_ft, path.c_str(), 0, &face)) {
            std::string family = face->family_name ? face->family_name : "(unknown)";
            std::string style  = face->style_name  ? face->style_name  : "(unknown)";
            // std::cout << "[FONT] Found " << family << " " << style << std::endl;
            discovered_fonts[family + " " + style] = path;
            FT_Done_Face(face);
        }
        }
    }
    {
        std::lock_guard lock(s_system_fonts_mutex);
        s_system_fonts = std::move(discovered_fonts);
    }
}

namespace {
    bgui::font build_font_atlas(
        const std::string& font_name,
        const std::string& font_path,
        const unsigned int resolution
    ) {
        if (resolution == 0)
            throw std::invalid_argument("Font resolution must be greater than zero.");

        FT_Face face;
        if (FT_New_Face(s_ft, font_path.c_str(), 0, &face))
            throw std::runtime_error("Error loading font: " + font_name);

        struct face_guard {
            FT_Face value;
            ~face_guard() { FT_Done_Face(value); }
        } guard{face};

        FT_Set_Pixel_Sizes(face, 0, resolution);
        bgui::detail::log_out() << "[FONT] Loading: " << font_name << " (" << font_path << ")\n";

        bgui::font font{};
        font.atlas.m_path = font_path;
        font.atlas.m_use_red_channel = true;
        font.atlas.m_generate_mipmap = false;
        font.family = face->family_name ? face->family_name : font_name;
        font.style = face->style_name ? face->style_name : "regular";
        font.resolution = resolution;

        const int padding = 4;
        const int ascent_pixels = face->size->metrics.ascender / 64;
        const int descent_pixels = -face->size->metrics.descender / 64;
        const float ascent_float = face->size->metrics.ascender / 64.0f;
        const float descent_float = -face->size->metrics.descender / 64.0f;
        const float linegap =
            face->size->metrics.height / 64.0f - (ascent_float + descent_float);
        font.ascent = ascent_float;
        font.descent = descent_float;
        font.line_gap = linegap;

        int atlas_width = 0;
        const int atlas_height = ascent_pixels + descent_pixels;
        for (unsigned int codepoint = 32; codepoint < 256; ++codepoint) {
            if (FT_Load_Char(face, codepoint, FT_LOAD_DEFAULT))
                continue;
            atlas_width += face->glyph->metrics.width / 64 + padding;
        }

        font.atlas.m_buffer.assign(atlas_width * atlas_height, 0);
        bgui::detail::log_out() << "[FONT] Atlas size: " << atlas_width
                                << " x " << atlas_height << "\n";

        int x_offset = 0;
        for (unsigned int codepoint = 32; codepoint < 256; ++codepoint) {
            if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER))
                continue;

            FT_Bitmap& bitmap = face->glyph->bitmap;
            const int bitmap_width = bitmap.width;
            const int bitmap_height = bitmap.rows;
            const int y_offset = ascent_pixels - face->glyph->bitmap_top;
            font.atlas.m_offset[1] = y_offset;
            font.atlas.m_offset[0] = y_offset;

            bgui::character character{};
            character.size = {
                static_cast<unsigned int>(bitmap_width),
                static_cast<unsigned int>(bitmap_height)
            };
            character.bearing = {
                face->glyph->bitmap_left,
                face->glyph->bitmap_top
            };
            character.advance = face->glyph->advance.x >> 6;
            character.uv_min = {
                static_cast<float>(x_offset) / atlas_width,
                static_cast<float>(y_offset + bitmap_height) / atlas_height
            };
            character.uv_max = {
                static_cast<float>(x_offset + bitmap_width) / atlas_width,
                static_cast<float>(y_offset) / atlas_height
            };
            font.chs.emplace(codepoint, character);

            for (int row = 0; row < bitmap_height; ++row) {
                for (int column = 0; column < bitmap_width; ++column) {
                    const int atlas_x = x_offset + column;
                    const int atlas_y = y_offset + row;
                    if (atlas_y < 0 || atlas_y >= atlas_height ||
                        atlas_x < 0 || atlas_x >= atlas_width)
                        continue;

                    const unsigned char pixel_value = bitmap.buffer[row * bitmap.pitch + column];
                    font.atlas.m_buffer[atlas_y * atlas_width + atlas_x] = pixel_value;
                }
            }
            x_offset += bitmap_width + padding;
        }

        font.atlas.m_size = {
            static_cast<float>(atlas_width),
            static_cast<float>(atlas_height)
        };
        font.atlas.m_revision = 1;
        return font;
    }

    bgui::font& store_built_font(
        const std::string& font_name,
        const unsigned int resolution,
        bgui::font value
    ) {
        auto& manager = bgui::font_manager::get_instance();
        auto& stored_font = bgui::asset_manager::get_instance().store_font(
            font_name, resolution, std::move(value));
        if (!manager.has_font("default", resolution))
            manager.set_default_font(font_name, resolution);
        if (manager.m_on_font_loaded)
            manager.m_on_font_loaded(stored_font);
        bgui::detail::log_out() << "[FONT] Loaded and cached: "
                                << bgui::font_manager::make_key(font_name, resolution) << "\n";
        return stored_font;
    }
}

bgui::font& bgui::ft_load_font(
    const std::string& font_name,
    const std::string& font_path,
    const unsigned int resolution
) {
    auto& manager = bgui::font_manager::get_instance();
    if (resolution == 0)
        throw std::invalid_argument("Font resolution must be greater than zero.");

    const std::string key = bgui::font_manager::make_key(font_name, resolution);
    if (manager.has_font(font_name, resolution)) {
        bgui::detail::log_out() << "[FONT] Using cached font: " << key << "\n";
        return manager.get_font(font_name, resolution);
    }

    bgui::font loaded;
    {
        std::lock_guard lock(s_ft_mutex);
        loaded = build_font_atlas(font_name, font_path, resolution);
    }
    return store_built_font(font_name, resolution, std::move(loaded));
}

void bgui::load_font_queue() {
    auto& loader = get_font_loader();
    font_load_result result;
    while (loader.take_completed(result)) {
        if (bgui::font_manager::get_instance().has_font(
                result.request.name, result.request.resolution))
            continue;
        if (result.error)
            std::rethrow_exception(result.error);
        store_built_font(
            result.request.name,
            result.request.resolution,
            std::move(result.value)
        );
    }

    auto& queue = bgui::font_manager::get_instance().m_font_queue;
    while (!queue.empty()) {
        const auto name = std::move(queue.front());
        queue.pop();
        if (name == "default" || bgui::font_manager::get_instance().has_font(name))
            continue;

        std::string path;
        {
            std::lock_guard lock(s_system_fonts_mutex);
            const auto font = s_system_fonts.find(name);
            if (font != s_system_fonts.end())
                path = font->second;
        }
        if (!path.empty()) {
            loader.enqueue({
                name,
                std::move(path),
                bgui::font_manager::m_default_resolution
            });
        }
    }
}