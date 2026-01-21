#include <string>
#include <map>
#include <utility>
#include <stdexcept>
#include <iostream>
#include <memory>
#include <gl3/gl3_shader.hpp>
#include "gl3_shader.hpp"

using namespace bgl;

static std::map<std::pair<std::string, std::string>, GLuint> gl3_shader_cache;
static std::map<std::string, std::string> embedded_gl3_shaders = {
{"ui::default_vs", R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;

out vec2 Uv;

uniform vec4 rect;
uniform mat4 projection;

void main() {
    vec2 pos = aPos * rect.zw + rect.xy;
    Uv = aUv;
    gl_Position =  projection * vec4(pos, 0, 1);
}
)"},
{"ui::default_fs", R"(#version 330 core

in vec2 Uv; // UV coordinates from vertex gl3_shader (0..1)
out vec4 FragColor;

// uniforms
uniform vec4 bg_color;
uniform vec4 border_color;
uniform bool bordered;
uniform float border_radius;
uniform float border_size;
uniform vec4 rect;

void main() {
    if(bordered) {
        vec2 pos = rect.zw * Uv;
        vec2 halfSize = rect.zw / 2;
        vec2 center = rect.zw * 0.5;
    vec2 p = pos - center;

    vec2 q = abs(p) - (center - vec2(border_radius));
    float dist = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - border_radius;

        if(dist > 0.0) discard;
        if (dist > -border_size && bordered) {
            FragColor = border_color;
        } else {
            FragColor = bg_color;
        }
    } else {
        FragColor = bg_color;
    }
}
)"}, {"ui::text_fs", R"(#version 330 core

in vec2 Uv;
out vec4 FragColor;

uniform sampler2D tex;
uniform vec4 text_color;     // RGBA text color
uniform vec2 uv_min;
uniform vec2 uv_max;

void main() {
    vec2 uv = mix(uv_min, uv_max, vec2(Uv.x, Uv.y));

    float r = texture(tex, uv).r;

    FragColor = vec4(text_color.rgb, r * text_color.w);
}
)"}
};

gl3_shader::gl3_shader(const char * vertex_path, const char * fragment_path) {
    compile(vertex_path, fragment_path);
}

void gl3_shader::compile(const char* vertex_path, const char* fragment_path) {
    std::pair <std::string, std::string> key = {vertex_path, fragment_path};
    auto it = gl3_shader_cache.find(key);
    if (it != gl3_shader_cache.end()) {
        m_id = gl3_shader_cache.at(key);
        return;
    }

    // Uses default gl3_shader if don't found in embedded gl3_shaders
    auto source_v = embedded_gl3_shaders.find(key.first) != embedded_gl3_shaders.end() ? embedded_gl3_shaders[key.first] : embedded_gl3_shaders["ui::default_vs"];
    auto source_f = embedded_gl3_shaders.find(key.second) != embedded_gl3_shaders.end() ? embedded_gl3_shaders[key.second] : embedded_gl3_shaders["ui::default_fs"];
    GLuint vert = compile(GL_VERTEX_SHADER, source_v);
    GLuint frag = compile(GL_FRAGMENT_SHADER, source_f);

    GLuint program = link(vert, frag);
    m_id = program;
    gl3_shader_cache[key] = program;
}

gl3_shader::~gl3_shader() {
}

GLuint gl3_shader::compile(GLenum type, const std::string &source) {
    GLuint sh = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);

    GLint success = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint logLen = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &logLen);
        std::string infoLog(logLen > 0 ? logLen : 1, '\0');
        glGetShaderInfoLog(sh, logLen, nullptr, &infoLog[0]);

        // cleanup shader object to avoid leak
        glDeleteShader(sh);

        throw std::runtime_error(
            std::string("gl3_shader compilation failed (") +
            (type == GL_VERTEX_SHADER ? "vertex" : "fragment") +
            "): " + infoLog
        );
    }

    return sh;
}

GLuint gl3_shader::link(GLuint vert, GLuint frag) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint success = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        std::string infoLog(logLen > 0 ? logLen : 1, '\0');
        glGetProgramInfoLog(prog, logLen, nullptr, &infoLog[0]);

        // cleanup both shaders and program
        glDeleteProgram(prog);
        glDeleteShader(vert);
        glDeleteShader(frag);

        throw std::runtime_error("gl3_shader link failed:\n" + infoLog);
    }

    // shaders can be deleted after successful link
    glDeleteShader(vert);
    glDeleteShader(frag);

    return prog;
}

void bgl::gl3_shader::set(const std::string& name, const bgui::propertie u) {
    switch (u.m_type) {
    case 0x0: // vec2
        set_vec2(name.c_str(), u.m_value.m_vec2);
        break;
    case 0x1: // vec3
        set_vec3(name.c_str(), u.m_value.m_vec3);
        break;
    case 0x2: // vec4
        set_vec4(name.c_str(), u.m_value.m_vec4);
        break;
    case 0x3: // vec4i
        set_vec4(name.c_str(), u.m_value.m_vec4i);
        break;
    case 0x4: // mat4
        set_mat4(name.c_str(), u.m_value.m_mat4);
        break;
    case 0x5: // float
        set_float(name.c_str(), u.m_value.m_float);
        break;
    case 0x6: // int
        set_int(name.c_str(), u.m_value.m_int);
        break;
    
    default:
        break;
    }
}
void gl3_shader::set_mat4(const char *name, const bgui::mat4 matrix) {
    GLint loc = glGetUniformLocation(m_id, name);
    if (loc >= 0)
    glUniformMatrix4fv(loc, 1, GL_FALSE, matrix.data());
}

void gl3_shader::set_vec4(const char *name, const bgui::vec4 vector) {
    GLint loc = glGetUniformLocation(m_id, name);
    if (loc >= 0)
    glUniform4f(loc, vector[0], vector[1], vector.z, vector.w);
}

void gl3_shader::set_vec4(const char *name, const bgui::vec4i vector) {
    GLint loc = glGetUniformLocation(m_id, name);
    if (loc >= 0)
    glUniform4i(loc, vector[0], vector[1], vector.z, vector.w);
}

void gl3_shader::set_vec3(const char *name, const bgui::vec3 vector) {
    GLint loc = glGetUniformLocation(m_id, name);
    if (loc >= 0)
    glUniform3f(loc, vector[0], vector[1], vector.z);
}

void gl3_shader::set_vec2(const char *name, const bgui::vec2 vector) {
    GLint loc = glGetUniformLocation(m_id, name);
    if (loc >= 0)
    glUniform2f(loc, vector[0], vector[1]);
}
    
void gl3_shader::set_bool(const char* name, const bool v) {
    GLint loc = glGetUniformLocation(m_id, name);
    if (loc >= 0)
    glUniform1i(loc, v);
}
        
void gl3_shader::set_int(const char* name, const int v) {
    GLint loc = glGetUniformLocation(m_id, name);
    if (loc >= 0)
    glUniform1i(loc, v);
}        

void gl3_shader::set_float(const char* name, const float v) {
    GLint loc = glGetUniformLocation(m_id, name);
    if (loc >= 0)
    glUniform1f(loc, v);
}

void gl3_shader::bind() {
    if(m_id == 0) {
        throw std::runtime_error("Invalid id! Have you compiled before binding?\n");
        return;
    }
    glUseProgram(m_id);
}

void gl3_shader::unbind() {
    glUseProgram(0);
}

gl3_shader* bgl::get_default_gl3_shader() {
    static gl3_shader s("ui::default_vs", "ui::default_fs");
    return &s;
}

gl3_shader* bgl::get_text_gl3_shader() {
    static gl3_shader s("ui::default_vs", "ui::text_fs");
    return &s;
}

gl3_shader* bgl::get_gl3_shader_from_tag(const std::string& name) {
    if(name == "ui::default") {
        return bgl::get_default_gl3_shader();
    } else if(name == "ui::text") {
        return bgl::get_text_gl3_shader();
    } else {
        return bgl::get_default_gl3_shader();
    }
}
void bgl::delete_gl3_shaders() {
    for (auto& pair : gl3_shader_cache) {
        glDeleteProgram(pair.second);
    }
    gl3_shader_cache.clear();
    std::cout << "BGUI: Deleted all GL3 shaders from cache." << std::endl;
}