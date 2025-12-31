#pragma once
#include <os/font.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace bgui {
    void set_up_freetype();
    void shutdown_freetype();
    bgui::font& ft_load_font(const std::string& font_name, const std::string& font_path, unsigned int resolution);
    bgui::font& ft_load_system_font(const std::string& font_name);
    void ft_search_system_fonts(const std::string& filter = "");
} // namespace bgui