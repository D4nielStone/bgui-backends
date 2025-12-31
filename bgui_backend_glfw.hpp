#pragma once
#include <os/os.hpp>
#include <GLFW/glfw3.h>

namespace bgui {
    GLFWwindow* set_up_glfw(int width, int height, const char* title, int flags = 0, GLFWmonitor* monitor = nullptr, GLFWwindow* share = nullptr);
    void glfw_update(bgui::context &window_io);
    void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    void shutdown_glfw();
    bool should_close_glfw();
    void swap_glfw();
} // namespace bgui