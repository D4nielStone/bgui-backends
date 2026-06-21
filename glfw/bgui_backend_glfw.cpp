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
static std::unordered_map<int, bgui::input_key> s_glfw_keyboard_reverse_map = {
    {GLFW_KEY_BACKSPACE, bgui::input_key::backspace},
    {GLFW_KEY_ENTER, bgui::input_key::enter},
};
static std::unordered_map<int, bgui::input_action> s_glfw_action_reverse_map = {
    {GLFW_PRESS, bgui::input_action::press},
    {GLFW_RELEASE, bgui::input_action::release},
    {GLFW_REPEAT, bgui::input_action::repeat}
};

static std::string utf32_to_utf8(char32_t ch) {
    std::string out;
    if (ch <= 0x7F) {
        out.push_back(static_cast<char>(ch));
    } else if (ch <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((ch >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else if (ch <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((ch >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | ((ch >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
    }
    return out;
}

void bgui::glfw_framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    bgui::get_context().m_size = bgui::vec2i{width, height};
}
void bgui::glfw_window_refresh_callback(GLFWwindow* window) {
    if(bgui::get_context().m_refresh_func) {
        bgui::get_context().m_refresh_func();
    }
}


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
    glfwSetKeyCallback(window, bgui::glfw_key_callback);
    glfwSetCharCallback(window, bgui::glfw_char_callback);
    glfwSetFramebufferSizeCallback(window, bgui::glfw_framebuffer_size_callback);
    glfwSetWindowRefreshCallback(window, bgui::glfw_window_refresh_callback);

    glfwMakeContextCurrent(window);
    // Disable VSync for more accurate FPS measurement
    glfwSwapInterval(0);
    
    return window;
}

void bgui::glfw_update(bgui::context &window_io) {
    int width, height;
    glfwGetWindowSize(s_window, &width, &height);
    window_io.m_size[0] = width;
    window_io.m_size[1] = height;

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
    auto it = s_glfw_key_reverse_map.find(button);
    if (it == s_glfw_key_reverse_map.end()) return;

    bgui::input_key internal_key = it->second;
    bgui::input_action internal_action = static_cast<bgui::input_action>(s_glfw_action_reverse_map.at(action));
    bgui::get_context().m_input_map[internal_key] = internal_action;
}

void bgui::glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto it = s_glfw_keyboard_reverse_map.find(key);
    if (it == s_glfw_keyboard_reverse_map.end()) return;

    bgui::input_key internal_key = it->second;
    bgui::input_action internal_action = static_cast<bgui::input_action>(s_glfw_action_reverse_map.at(action));
    bgui::get_context().m_input_map[internal_key] = internal_action;
}

void bgui::glfw_char_callback(GLFWwindow* window, unsigned int codepoint) {
    if (codepoint == 0) return;
    bgui::get_context().m_char_buffer += utf32_to_utf8(static_cast<char32_t>(codepoint));
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
