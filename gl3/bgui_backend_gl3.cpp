#include <glad/glad.h>
#include "bgui_backend_gl3.hpp"
#include "gl3_shader.hpp"
#include <bgui.hpp>
#include <os/os.hpp>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <memory>
#include <string>
#include <sstream>
#include <algorithm>

#define DEBUG_COLOR_R 255
#define DEBUG_COLOR_G 0
#define DEBUG_COLOR_B 255
#define DEBUG_COLOR_A 255

// RAII wrapper for VAO/VBO
struct quad_vao {
    GLuint vao = 0;
    GLuint vbo = 0;

    quad_vao() {
        // simple full-quad (2 triangles) with position (x,y) and texcoord (u,v)
        float quad_vertices[] = {
            // positions   // texcoords
            0.0f, 1.0f,   0.0f, 1.0f,
            1.0f, 0.0f,   1.0f, 0.0f,
            0.0f, 0.0f,   0.0f, 0.0f,
            0.0f, 1.0f,   0.0f, 1.0f,
            1.0f, 1.0f,   1.0f, 1.0f,
            1.0f, 0.0f,   1.0f, 0.0f
        };

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

        // attribute 0: vec2 position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

        // attribute 1: vec2 texcoord
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        // restore to safe default
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    ~quad_vao() {
        // Unbind before deletion to avoid leaving references to deleted objects in state
        GLint boundVao = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &boundVao);
        if ((GLuint)boundVao == vao) glBindVertexArray(0);

        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
    }
};

// Texture cache: map key -> GLuint
static std::unordered_map<std::string, GLuint> m_texture_cache;

// Singleton VAO (recreated if needed)
static std::unique_ptr<quad_vao> s_quad_vao;

// Helper: build a cache key robust to same path but different buffer/flags
static std::string build_texture_cache_key(const bgui::texture& tex) {
    // Combine path, buffer size, alpha flag, red-channel flag
    std::ostringstream ss;
    ss << tex.m_path << "|" << tex.m_buffer.size()
       << "|" << (tex.m_has_alpha ? "A" : "N")
       << "|" << (tex.m_use_red_channel ? "R" : "C")
       << "|" << (tex.m_generate_mipmap ? "M" : "N");
    return ss.str();
}

/**
 * @brief Verify if all bytes in the buffer are zero.
 * @param buffer the buffer
 * @return true if all the bytes in the buffer are zero, false otherwise.
 */
static bool is_buffer_all_zeros(const std::vector<unsigned char>& buffer) {
    if (buffer.empty()) return true;
    
    // Optimization: look for the first byte that is NOT zero.
    // If none is found, the buffer is all zeros.
    return std::find_if(buffer.begin(), buffer.end(), [](unsigned char c) {
        return c != 0;
    }) == buffer.end();
}

GLuint bgui::get_quad_vao() {
    if (!s_quad_vao) {
        s_quad_vao = std::make_unique<quad_vao>();
    }
    return s_quad_vao->vao;
}

// Safe function to obtain/generate texture (fixed with DEBUG logic)
GLuint bgui::gl3_get_texture(const bgui::texture& tex) {

    const std::string key = build_texture_cache_key(tex);
    auto it = m_texture_cache.find(key);
    if (it != m_texture_cache.end()) return it->second;

    GLuint texture_id = 0;
    glGenTextures(1, &texture_id);
    glBindTexture(GL_TEXTURE_2D, texture_id);

    // basic wrap / filter for UI; caller can change if needed
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, tex.m_generate_mipmap ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (!tex.m_buffer.empty()) {
        
        bool debug_upload = false;
        
        // Debug check: if the texture is a font atlas and the buffer is all zeros, use debug texture
        if (tex.m_use_red_channel && is_buffer_all_zeros(tex.m_buffer)) {
            std::cerr << "[GL3 DEBUG] WARNING: Font atlas buffer (" << tex.m_path << ") is all zeros! Displaying debug texture.\n";
            debug_upload = true;
        }

        GLsizei width  = static_cast<GLsizei>(tex.m_size[0]);
        GLsizei height = static_cast<GLsizei>(tex.m_size[1]);

        if (debug_upload) {
            GLenum internalFormat = GL_RGBA8;
            GLenum format = GL_RGBA;
            
            unsigned char debug_pattern[16] = {
                DEBUG_COLOR_R, DEBUG_COLOR_G, DEBUG_COLOR_B, DEBUG_COLOR_A,
                0, 0, 0, 255,                                              
                0, 0, 0, 255,                                              
                DEBUG_COLOR_R, DEBUG_COLOR_G, DEBUG_COLOR_B, DEBUG_COLOR_A 
            };
            
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, 2, 2, 0, format, GL_UNSIGNED_BYTE, debug_pattern);
            
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            
        } else {
            
            GLenum internalFormat;
            GLenum format;
            
            if (tex.m_use_red_channel) {
                internalFormat = GL_R8;
                format = GL_RED;
            } else if (tex.m_has_alpha) {
                internalFormat = GL_RGBA8;
                format = GL_RGBA;
            } else {
                internalFormat = GL_RGB8;
                format = GL_RGB;
            }
            
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, tex.m_buffer.data());
            
            if (tex.m_generate_mipmap) {
                glGenerateMipmap(GL_TEXTURE_2D);
            }
        }
    } else {
        unsigned char white_pixel[4] = {255, 255, 255, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_pixel);
    }
    m_texture_cache[key] = texture_id;

    // unbind texture for hygiene
    glBindTexture(GL_TEXTURE_2D, 0);

    return texture_id;
}

// Clear texture cache (delete GL textures)
void bgui::gl3_clear_texture_cache() {
    for (auto& kv : m_texture_cache) {
        GLuint id = kv.second;
        if (id) glDeleteTextures(1, &id);
    }
    m_texture_cache.clear();
}

// gl3 initial setup
void bgui::set_up_gl3() {
    std::cout << "[OpenGL3 BackEnd] Setting up OpenGL.\n";
    // gladLoadGL returns non-zero on success (depending on your glad configuration).
    // If you use gladLoadGLLoader you need to call gladLoadGLLoader((GLADloadproc)glfwGetProcAddress) earlier.
    if (gladLoadGL())
        std::cout << "[OpenGL3 BackEnd] OpenGL loaded successfully!" << std::endl;
    else
        std::cout << "[OpenGL3 BackEnd] Failed to initialize OpenGL context" << std::endl;    

    // default pixel unpack alignment for arbitrary image widths
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // UI common defaults
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Disable depth & cull for 2D UI; if other parts of engine need depth/cull they should set those states themselves.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

// Shutdown safely - must be called while OpenGL context is valid
void bgui::shutdown_gl3() {
    // delete textures
    gl3_clear_texture_cache();

    // reset and delete VAO/VBO
    s_quad_vao.reset();
}

// Render main
void bgui::gl3_render(bgui::draw_data* data) {
    if(data->m_quad_requires.empty()) throw std::runtime_error("Nothing to render. Have you update the interface?");
    // basic clear
    auto& sm = style_manager::get_instance();
    const auto& global = sm.get_global();

    const auto window_size = bgui::get_context_size();
    glViewport(0, 0, window_size[0], window_size[1]);
    if(global.visual.background.normal) {
        const auto& bg = *global.visual.background.normal;
        glClearColor(bg.r, bg.g, bg.b, bg.a);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    // Ensure VAO exists (recreate if needed)
    glBindVertexArray(get_quad_vao());

    const bgui::mat4 proj = bgui::get_projection();

    // We'll avoid binding/unbinding shader for each quad:
    // track last shader to reduce state changes
    bgl::gl3_shader* last_shader = nullptr;

    // process all quad requires
    while (!data->m_quad_requires.empty()) {
        auto call = data->m_quad_requires.front();
        data->m_quad_requires.pop();

        auto* shader = bgl::get_gl3_shader_from_tag(call.m_material.m_shader_tag);
        if (!shader) continue;

        // Bind shader only if it's different from last used
        if (shader != last_shader) {
            if (last_shader) last_shader->unbind();
            shader->bind();
            last_shader = shader;
        }

        if (call.m_material.m_use_tex) {
            glActiveTexture(GL_TEXTURE0);
            GLuint texid = gl3_get_texture(call.m_material.m_texture);
            glBindTexture(GL_TEXTURE_2D, texid);
            shader->set("tex", 0); // sampler unit 0
        } else {
            // make sure no texture bound if material doesn't want texture (avoid sampling mistakes)
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // bind properties (uniforms)
        for (auto& prop : call.m_material.m_properties) {
            // shader->set should be implemented to set various uniform types
            shader->set(prop.first, prop.second);
        }

        // set quad uniforms
        shader->set("rect", call.m_rect);
        shader->set("uv_min", call.m_uv_min);
        shader->set("uv_max", call.m_uv_max);
        shader->set("projection", proj);

        // draw: call.m_count should be number of vertices (6 for quad)
        glDrawArrays(GL_TRIANGLES, 0, call.m_count);
    }

    // unbind last shader and VAO
    if (last_shader) last_shader->unbind();
    glBindVertexArray(0);

    // unbind texture for hygiene
    glBindTexture(GL_TEXTURE_2D, 0);
}