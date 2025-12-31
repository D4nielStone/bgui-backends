#pragma once
#include <glad/glad.h>
#include <os/os.hpp>
#include <memory>
#include <utils/mat.hpp>
#include <utils/vec.hpp>
#include <utils/propertie.hpp>

namespace bgl {
    class gl3_shader {
    private:
        GLuint m_id;
        
        void set_mat4(const char* name, const bgui::mat4 matrix);
        void set_vec4(const char* name, const bgui::vec4 vector);
        void set_vec4(const char* name, const bgui::vec4i vector);
        void set_vec3(const char* name, const bgui::vec3 vector);
        void set_vec2(const char* name, const bgui::vec2 vector);
        void set_bool(const char* name, const bool v);
        void set_int(const char* name, const int v);
        void set_float(const char* name, const float v);
    public:
        gl3_shader() = default;
        gl3_shader(const char* vertex_path, const char* fragment_path);
        ~gl3_shader();
        void bind();
        void unbind();

        void compile(const char* vertex_path, const char* fragment_path);
        static GLuint compile(GLenum type, const std::string& source);
        static GLuint link(GLuint vert, GLuint frag);

        void set(const std::string& name, const bgui::propertie u);

        bool operator==(const gl3_shader& other) const {
            return m_id == other.m_id;
        }
    };
    gl3_shader* get_default_gl3_shader();
    gl3_shader* get_text_gl3_shader();
    gl3_shader* get_gl3_shader_from_tag(const std::string&);
    static void delete_gl3_shaders();
} // namespace bgl