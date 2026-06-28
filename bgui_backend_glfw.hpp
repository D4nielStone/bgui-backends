#pragma once
#include <os/os.hpp>
#include <GLFW/glfw3.h>

namespace bgui {
    GLFWwindow* set_up_glfw(int width, int height, const char* title, int flags = 0, GLFWmonitor* monitor = nullptr, GLFWwindow* share = nullptr);
    void add_glfw_window_position(int x, int y);
    void glfw_main_loop();
    void glfw_update(bgui::context &window_io);
    void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    void glfw_char_callback(GLFWwindow* window, unsigned int codepoint);
    void glfw_framebuffer_size_callback(GLFWwindow* window, int width, int height);
    void glfw_window_refresh_callback(GLFWwindow* window);
    void set_glfw_window_decoration(bool b);
    void set_glfw_window_position(int x, int y);
    void shutdown_glfw();
    bool should_close_glfw();
    void swap_glfw();
} // namespace bgui