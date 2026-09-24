#include <bgui_backend_glfw.hpp>
#include <os/os.hpp>
#include <stdexcept>
#include <iostream>

static GLFWwindow* s_window = nullptr;

// -----------------------------------------------------------------------------
// Maps
// -----------------------------------------------------------------------------

static std::unordered_map<int, bgui::input_key> s_glfw_key_reverse_map = {
    {GLFW_MOUSE_BUTTON_LEFT, bgui::input_key::mouse_left},
    {GLFW_MOUSE_BUTTON_RIGHT, bgui::input_key::mouse_right},
    {GLFW_MOUSE_BUTTON_MIDDLE, bgui::input_key::mouse_middle}
};

static std::unordered_map<int, bgui::input_key> s_glfw_keyboard_reverse_map = {
    {GLFW_KEY_BACKSPACE, bgui::input_key::backspace},
    {GLFW_KEY_ENTER, bgui::input_key::enter},
    {GLFW_KEY_LEFT, bgui::input_key::left},
    {GLFW_KEY_RIGHT, bgui::input_key::right},
    {GLFW_KEY_ESCAPE, bgui::input_key::escape},
    {GLFW_KEY_Q, bgui::input_key::q}, {GLFW_KEY_W, bgui::input_key::w},
    {GLFW_KEY_E, bgui::input_key::e}, {GLFW_KEY_R, bgui::input_key::r},
    {GLFW_KEY_T, bgui::input_key::t}, {GLFW_KEY_Y, bgui::input_key::y},
    {GLFW_KEY_U, bgui::input_key::u}, {GLFW_KEY_I, bgui::input_key::i},
    {GLFW_KEY_O, bgui::input_key::o}, {GLFW_KEY_P, bgui::input_key::p},
    {GLFW_KEY_A, bgui::input_key::a}, {GLFW_KEY_S, bgui::input_key::s},
    {GLFW_KEY_D, bgui::input_key::d}, {GLFW_KEY_F, bgui::input_key::f},
    {GLFW_KEY_G, bgui::input_key::g}, {GLFW_KEY_H, bgui::input_key::h},
    {GLFW_KEY_J, bgui::input_key::j}, {GLFW_KEY_K, bgui::input_key::k},
    {GLFW_KEY_L, bgui::input_key::l}, {GLFW_KEY_Z, bgui::input_key::z},
    {GLFW_KEY_X, bgui::input_key::x}, {GLFW_KEY_C, bgui::input_key::c},
    {GLFW_KEY_V, bgui::input_key::v}, {GLFW_KEY_B, bgui::input_key::b},
    {GLFW_KEY_N, bgui::input_key::n}, {GLFW_KEY_M, bgui::input_key::m},
    {GLFW_KEY_UP, bgui::input_key::up}, {GLFW_KEY_DOWN, bgui::input_key::down},
    {GLFW_KEY_LEFT_SHIFT, bgui::input_key::left_shift},
    {GLFW_KEY_RIGHT_SHIFT, bgui::input_key::right_shift},
    {GLFW_KEY_LEFT_CONTROL, bgui::input_key::left_control},
    {GLFW_KEY_RIGHT_CONTROL, bgui::input_key::right_control},
    {GLFW_KEY_LEFT_ALT, bgui::input_key::left_alt},
    {GLFW_KEY_RIGHT_ALT, bgui::input_key::right_alt},
    {GLFW_KEY_DELETE, bgui::input_key::delete_key},
    {GLFW_KEY_F5, bgui::input_key::f5},
    {GLFW_KEY_KP_ENTER, bgui::input_key::keypad_enter},
};

static std::unordered_map<int, bgui::input_action> s_glfw_action_reverse_map = {
    {GLFW_PRESS, bgui::input_action::press},
    {GLFW_RELEASE, bgui::input_action::release},
    {GLFW_REPEAT, bgui::input_action::repeat}
};

// -----------------------------------------------------------------------------
// UTF32 -> UTF8
// -----------------------------------------------------------------------------

static std::string utf32_to_utf8(char32_t ch)
{
    std::string out;

    if (ch <= 0x7F)
    {
        out.push_back((char)ch);
    }
    else if (ch <= 0x7FF)
    {
        out.push_back((char)(0xC0 | ((ch >> 6) & 0x1F)));
        out.push_back((char)(0x80 | (ch & 0x3F)));
    }
    else if (ch <= 0xFFFF)
    {
        out.push_back((char)(0xE0 | ((ch >> 12) & 0x0F)));
        out.push_back((char)(0x80 | ((ch >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (ch & 0x3F)));
    }
    else
    {
        out.push_back((char)(0xF0 | ((ch >> 18) & 0x07)));
        out.push_back((char)(0x80 | ((ch >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((ch >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (ch & 0x3F)));
    }

    return out;
}

// -----------------------------------------------------------------------------
// Callbacks
// -----------------------------------------------------------------------------

void bgui::glfw_framebuffer_size_callback(GLFWwindow*, int width, int height)
{
    auto& io = bgui::get_context();
    io.m_size = {width, height};
}

void bgui::glfw_window_refresh_callback(GLFWwindow*)
{
    auto& io = bgui::get_context();

    if (io.m_refresh_func)
        io.m_refresh_func();
}

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------

void bgui::attach_glfw_window(GLFWwindow* window)
{
    if (!window)
        throw std::invalid_argument("Cannot attach a null GLFW window.");

    if (s_window && s_window != window)
        throw std::runtime_error("GLFW window already exists.");

    s_window = window;

    auto& io = bgui::get_context();
    glfwGetWindowSize(s_window, &io.m_size.x, &io.m_size.y);
    glfwSetMouseButtonCallback(s_window, bgui::glfw_mouse_button_callback);
    glfwSetKeyCallback(s_window, bgui::glfw_key_callback);
    glfwSetCharCallback(s_window, bgui::glfw_char_callback);
    glfwSetFramebufferSizeCallback(s_window, bgui::glfw_framebuffer_size_callback);
    glfwSetWindowRefreshCallback(s_window, bgui::glfw_window_refresh_callback);
}

void bgui::detach_glfw_window()
{
    s_window = nullptr;
}

GLFWwindow* bgui::set_up_glfw(
    int width,
    int height,
    const char* title,
    int flags,
    GLFWmonitor* monitor,
    GLFWwindow* share)
{
    if (s_window)
        throw std::runtime_error("GLFW window already exists.");

    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW.");

#ifdef BGUI_USE_OPENGL
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#elif BGUI_USE_VULKAN
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#endif

    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    s_window = glfwCreateWindow(width, height, title, monitor, share);

    if (!s_window)
    {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window.");
    }

    auto& io = bgui::get_context();

    io.m_title = title;
    io.m_size = {width, height};

    glfwSetMouseButtonCallback(s_window, bgui::glfw_mouse_button_callback);
    glfwSetKeyCallback(s_window, bgui::glfw_key_callback);
    glfwSetCharCallback(s_window, bgui::glfw_char_callback);
    glfwSetFramebufferSizeCallback(s_window, bgui::glfw_framebuffer_size_callback);
    glfwSetWindowRefreshCallback(s_window, bgui::glfw_window_refresh_callback);

#ifdef BGUI_USE_OPENGL
    glfwMakeContextCurrent(s_window);
    glfwSwapInterval(0);
#endif

    return s_window;
}

// -----------------------------------------------------------------------------
// Update
// -----------------------------------------------------------------------------

void bgui::glfw_update(context& io)
{
    int width, height;
    glfwGetWindowSize(s_window, &width, &height);
    io.m_size = {width, height};

    int windowX, windowY;
    glfwGetWindowPos(s_window, &windowX, &windowY);

    double localX, localY;
    glfwGetCursorPos(s_window, &localX, &localY);

    io.m_mouse_position = {
        (int)localX,
        (int)localY
    };

    io.m_draginfo.mouse_screen = {
        windowX + (float)localX,
        windowY + (float)localY
    };

    static GLFWcursor* hand  = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    static GLFWcursor* arrow = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    static GLFWcursor* ibeam = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);

    switch (io.m_actual_cursor)
    {
        case bgui::cursor::arrow:
            glfwSetCursor(s_window, arrow);
            break;

        case bgui::cursor::hand:
            glfwSetCursor(s_window, hand);
            break;

        case bgui::cursor::ibeam:
            glfwSetCursor(s_window, ibeam);
            break;
    }
}

// -----------------------------------------------------------------------------
// Window helpers
// -----------------------------------------------------------------------------

void bgui::set_glfw_window_position(int x, int y)
{
    glfwSetWindowPos(s_window, x, y);
}

void bgui::add_glfw_window_position(int dx, int dy)
{
    int x, y;
    glfwGetWindowPos(s_window, &x, &y);
    glfwSetWindowPos(s_window, x + dx, y + dy);
}

void bgui::set_glfw_window_decoration(bool enabled)
{
    glfwSetWindowAttrib(
        s_window,
        GLFW_DECORATED,
        enabled ? GLFW_TRUE : GLFW_FALSE);
}

// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------

void bgui::glfw_main_loop()
{
    while (!glfwWindowShouldClose(s_window))
    {
        glfwPollEvents();

        if (bgui::get_context().m_refresh_func)
            bgui::get_context().m_refresh_func();
    }
}

// -----------------------------------------------------------------------------
// Input
// -----------------------------------------------------------------------------

void bgui::glfw_mouse_button_callback(
    GLFWwindow*,
    int button,
    int action,
    int mods)
{
    auto it = s_glfw_key_reverse_map.find(button);

    if (it == s_glfw_key_reverse_map.end())
        return;

    auto& io = bgui::get_context();

    io.m_input_map[it->second] =
        s_glfw_action_reverse_map.at(action);

    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        if (action == GLFW_PRESS)
        {
            int wx, wy;
            glfwGetWindowPos(s_window, &wx, &wy);

            double mx, my;
            glfwGetCursorPos(s_window, &mx, &my);

            io.m_draginfo.dragging = true;

            io.m_draginfo.start_window_pos = {
                (float)wx,
                (float)wy
            };

            io.m_draginfo.start_mouse_screen = {
                wx + (float)mx,
                wy + (float)my
            };

            
        io.m_draginfo.offset = {
            io.m_draginfo.mouse_screen.x - wx,
            io.m_draginfo.mouse_screen.y - wy
        };
        }
        else if (action == GLFW_RELEASE)
        {
            io.m_draginfo.dragging = false;
        }
    }
}

void bgui::glfw_key_callback(
    GLFWwindow*,
    int key,
    int scancode,
    int action,
    int mods)
{
    auto it = s_glfw_keyboard_reverse_map.find(key);

    if (it == s_glfw_keyboard_reverse_map.end())
        return;

    bgui::get_context().m_input_map[it->second] =
        s_glfw_action_reverse_map.at(action);
}

void bgui::glfw_char_callback(GLFWwindow*, unsigned int codepoint)
{
    if (codepoint == 0)
        return;

    bgui::get_context().m_char_buffer +=
        utf32_to_utf8((char32_t)codepoint);
}

// -----------------------------------------------------------------------------
// Shutdown
// -----------------------------------------------------------------------------

void bgui::shutdown_glfw()
{
    glfwDestroyWindow(s_window);
    s_window = nullptr;
    glfwTerminate();
}

bool bgui::should_close_glfw()
{
    return glfwWindowShouldClose(s_window);
}

void bgui::swap_glfw()
{
    glfwSwapBuffers(s_window);
}