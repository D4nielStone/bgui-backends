#include <bgui_backend_freetype.hpp>
#include <unordered_map>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <cctype>
#include <cstdlib>

static std::unordered_map<std::string, std::string> s_system_fonts;
static FT_Library s_ft;

void bgui::set_up_freetype() {
    std::cout << "[FreeType BackEnd]Setting up FreeType and searching for default fonts.\n";

    if (FT_Init_FreeType(&s_ft)) {
        throw std::runtime_error("Error initializing Freetype.");
    }

    std::cout << "[FreeType BackEnd] Initialized.\n";

    ft_search_system_fonts("ARIAL,Arial,arial");
    if (s_system_fonts.empty()) {
        std::cout << "[FreeType BackEnd] No Arial fonts found. Searching all system fonts.\n";
        ft_search_system_fonts();
    }

    for(auto font : s_system_fonts) {
        std::cout << " - " << font.first << ": " << font.second << "\n";
    }

    std::cout << "[FreeType BackEnd] Total system fonts found: " << s_system_fonts.size() << "\n";

    if (s_system_fonts.find("Arial CE-Bold") == s_system_fonts.end()) {
        std::cerr << "[FreeType BackEnd] WARNING: Default font not found. Trying another font instead.\n";
        if (s_system_fonts.empty())
            throw std::runtime_error("No system fonts were found.");
        ft_load_font(s_system_fonts.begin()->first, s_system_fonts.begin()->second,
                     bgui::font_manager::m_default_resolution);
    } else {
        std::cout << "[FreeType BackEnd] Loading default font: Arial CE-Bold\n";
        ft_load_font("Arial CE-Bold", s_system_fonts["Arial CE-Bold"],
                     bgui::font_manager::m_default_resolution);
    }
}

bgui::font& bgui::ft_load_system_font(const std::string& path) {
    // 1: Verify if it's font was already loaded
    if(font_manager::get_instance().has_font(path) || path == "default")
        return font_manager::get_instance().get_font(path);
    std::cout << "[FreeType BackEnd] Loading system font: " << path << "\n";

    if(s_system_fonts.find(path) != s_system_fonts.end())
    return ft_load_font(path, s_system_fonts[path],
                    bgui::font_manager::m_default_resolution);
    else
        return font_manager::get_instance().get_font("default");
}

void bgui::shutdown_freetype() {
    FT_Done_FreeType(s_ft);
    std::cout << "[FreeType BackEnd] Shutdown.\n";
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

    std::cout << "[FreeType BackEnd] Scanning fonts in:";
    for (const auto& folder : folders)
        std::cout << " " << folder.string();
    if (!filters.empty()) {
        std::cout << " with filters: ";
        for (auto& f : filters) std::cout << f << " ";
    }
    std::cout << "\n";

    // search recursivaly for font files
    s_system_fonts.clear();
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
            s_system_fonts[family + " " + style] = path;
            FT_Done_Face(face);
        }
        }
    }
}

bgui::font& bgui::ft_load_font(const std::string &font_name,
                               const std::string &font_path,
                               unsigned int resolution)
{
    auto &fmgr = bgui::font_manager::get_instance();
    if (resolution == 0)
        throw std::invalid_argument("Font resolution must be greater than zero.");

    const std::string key = bgui::font_manager::make_key(font_name, resolution);

    if (fmgr.has_font(font_name, resolution)) {
        std::cout << "[FONT] Using cached font: " << key << "\n";
        return fmgr.get_font(font_name, resolution);
    }

    // Load face
    FT_Face face;
    if (FT_New_Face(s_ft, font_path.c_str(), 0, &face))
        throw std::runtime_error("Error loading font: " + font_name);

    FT_Set_Pixel_Sizes(face, 0, resolution);

    std::cout << "[FONT] Loading: " << font_name << " (" << font_path << ")\n";

    bgui::font font{};
    font.atlas.m_path = font_path;
    font.atlas.m_use_red_channel = true;
    font.family = face->family_name ? face->family_name : font_name;
    font.style = face->style_name ? face->style_name : "regular";
    font.resolution = resolution;

    const int padding = 4;
    const int ascent_pixels  = face->size->metrics.ascender  / 64;
    const int descent_pixels = -face->size->metrics.descender / 64;

    const float ascent_float  = face->size->metrics.ascender  / 64.0f;
    const float descent_float = -face->size->metrics.descender / 64.0f;
    const float linegap = face->size->metrics.height / 64.0f - (ascent_float + descent_float);
    // -----------------------------------------------------------------

    font.ascent = ascent_float;
    font.descent = descent_float;
    font.line_gap = linegap;

    int atlas_width = 0;
    int atlas_height = ascent_pixels + descent_pixels; 

    for (unsigned int c = 32; c < 256; c++) {
        if (FT_Load_Char(face, c, FT_LOAD_DEFAULT)) 
            continue;
        atlas_width += face->glyph->metrics.width / 64 + padding;
    }

    font.atlas.m_buffer.assign(atlas_width * atlas_height, 0);

    std::cout << "[FONT] Atlas size: " << atlas_width << " x " << atlas_height << "\n";

    int xOffset = 0;

    // PASS 2: Copy bitmaps
    for (unsigned int c = 32; c < 256; c++) {

        if (FT_Load_Char(face, c, FT_LOAD_RENDER))
            continue;

        FT_Bitmap &bmp = face->glyph->bitmap;
        int bmp_w = bmp.width;
        int bmp_h = bmp.rows;
        int pitch = bmp.pitch; 
        
        int yOffset = ascent_pixels - face->glyph->bitmap_top; 
        font.atlas.m_offset[1] = yOffset;
        font.atlas.m_offset[0] = yOffset;

        bgui::character ch{};
        ch.size = { (unsigned int)bmp_w, (unsigned int)bmp_h };
        ch.bearing = { face->glyph->bitmap_left, face->glyph->bitmap_top };
        ch.advance = face->glyph->advance.x >> 6;

        float u0 = float(xOffset) / atlas_width;
        float u1 = float(xOffset + bmp_w) / atlas_width;
        
        float v0 = float(yOffset) / atlas_height;
        float v1 = float(yOffset + bmp_h) / atlas_height;

        ch.uv_min = { u0, v1 };
        ch.uv_max = { u1, v0 };

        font.chs.emplace(c, ch);

        // Copy bitmap → atlas (usando pitch)
        for (int row = 0; row < bmp_h; row++) {
            for (int col = 0; col < bmp_w; col++) {
                int atlas_x = xOffset + col;
                int atlas_y = yOffset + row;
                
                if (atlas_y < 0 || atlas_y >= atlas_height || atlas_x < 0 || atlas_x >= atlas_width) {
                    continue; 
                }

                unsigned char pixel_value = bmp.buffer[row * pitch + col];

                int atlas_index = atlas_y * atlas_width + atlas_x;
                font.atlas.m_buffer[atlas_index] = pixel_value;
            }
        }

        xOffset += bmp_w + padding;
    }

    font.atlas.m_size = { float(atlas_width), float(atlas_height) };

    FT_Done_Face(face);

    auto [font_it, inserted] = fmgr.m_fonts.emplace(key, std::move(font));
    if (!inserted)
        return font_it->second;

    if (!fmgr.has_font("default", resolution))
        fmgr.set_default_font(font_name, resolution);

    if (fmgr.m_on_font_loaded)
        fmgr.m_on_font_loaded(font_it->second);

    std::cout << "[FONT] Loaded and cached: " << key << "\n";


    return font_it->second;
}
void bgui::load_font_queue() {
    auto& queue = bgui::font_manager::get_instance().m_font_queue;
    while (!queue.empty()) {
        const auto name = std::move(queue.front());
        queue.pop();
        ft_load_system_font(name);
    }
}