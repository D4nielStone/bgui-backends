#pragma once
#include "utils/draw.hpp"
// That file give me too much work, so I prefer to keep it simple

namespace bgui {
    void gl3_render(bgui::draw_data*);
    void gl3_clear_texture_cache();
    void set_up_gl3();
    void shutdown_gl3();
    GLuint get_quad_vao();
    GLuint gl3_get_texture(const bgui::texture& tex);
} // namespace bgui