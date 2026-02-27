#include <bgui_backend_glfw.hpp>
#include <os/os.hpp>
#include <stdexcept>
#include <iostream>

static GLFWwindow* s_window{nullptr};

// Maps to input conversion
static std::unordered_map<int, bgui::input_key> s_glfw_key_reverse_map = {
    {GLFW_MOUSE_BUTTON_LEFT, bgui::input_key::mouse_left},
    {GLFW_MOUSE_BUTTON_RIGHT, bgui::input_key::mouse_right},
    {GLFW_MOUSE_BUTTON_MIDDLE, bgui::input_key::mouse_middle}
};
static std::unordered_map<int, bgui::input_action> s_glfw_action_reverse_map = {
    {GLFW_PRESS, bgui::input_action::press},
    {GLFW_RELEASE, bgui::input_action::release},
    {GLFW_REPEAT, bgui::input_action::repeat}
};

// GLFW Backend functions
GLFWwindow* bgui::set_up_glfw(int width, int height, const char* title, int flags, GLFWmonitor* monitor, GLFWwindow* share) {
    std::cout << "[GLFW BackEnd] Setting up GLFW and creating a window.\n";
    if(s_window) 
    throw std::runtime_error("GLFW wind already exists.");
    if (!glfwInit()) {
        throw std::runtime_error("Failed to init GLFW\n");
    }
    #ifdef BGUI_USE_OPENGL
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    #elif BGUI_USE_VULKAN
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    #endif
    GLFWwindow* window = glfwCreateWindow(width, height, title, monitor, share);
    s_window = window;

    if (!window) {
        throw std::runtime_error("Failed to create window\n");
        glfwTerminate();
    }

    bgui::get_context().m_title = title;
    bgui::get_context().m_size = bgui::vec2i{width, height};
    glfwSetMouseButtonCallback(window, bgui::glfw_mouse_button_callback);

    glfwMakeContextCurrent(window);
    return window;
}
void bgui::glfw_update(bgui::context &window_io) {
    glfwPollEvents();

    // Gests the window size
    int width, height;
    glfwGetWindowSize(s_window, &width, &height);
    window_io.m_size[0] = width;
    window_io.m_size[1] = height;

    // Gests the mouse position
    double x, y;
    glfwGetCursorPos(s_window, &x, &y);
    window_io.m_mouse_position = bgui::vec2i{(int)x, (int)y};

    static GLFWcursor* handCursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    static GLFWcursor* arrowCursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    static GLFWcursor* ibeamCursor = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);

    switch(window_io.m_actual_cursor) {
        case bgui::cursor::arrow:
            glfwSetCursor(s_window, arrowCursor);
        break;
        case bgui::cursor::hand:
            glfwSetCursor(s_window, handCursor);
        break;
        case bgui::cursor::ibeam:
            glfwSetCursor(s_window, ibeamCursor);
        break;
    }
}

void bgui::glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    bgui::input_key internal_key = s_glfw_key_reverse_map.at(button);

    bgui::input_action internal_action = static_cast<bgui::input_action>(s_glfw_action_reverse_map.at(action)); 

    bgui::get_context().m_input_map[internal_key] = internal_action;
}
void bgui::shutdown_glfw() {
    glfwDestroyWindow(s_window);
    s_window = nullptr;
    glfwTerminate();
}

bool bgui::should_close_glfw() {
    if(!s_window)
        throw std::runtime_error("[BGUI] GLFW::Please Setup GLFW First!");
    return glfwWindowShouldClose(s_window);
}

void bgui::swap_glfw() {
    glfwSwapBuffers(s_window);
}