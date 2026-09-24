#pragma once
#include "utils/draw.hpp"
// That file give me too much work, so I prefer to keep it simple

namespace bgui {
    void gl3_render(bgui::draw_data*);
    std::string &get_gl_version();
    std::string &get_gl_vendor();
    std::string get_glsl_version();
    void gl3_clear_texture_cache();
    // Enables linear font-atlas filtering when true, or nearest filtering when false.
    void set_font_antialiasing(bool enabled);
    void set_up_gl3();
    void shutdown_gl3();
    // clear the screen based on the style manager's global background color
    void gl3_clear();
    GLuint get_quad_vao();
    GLuint gl3_get_texture(const bgui::texture& tex);
} // namespace bgui