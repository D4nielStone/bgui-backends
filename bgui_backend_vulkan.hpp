#pragma once
#include <vulkan/vulkan.h>
#include "utils/draw.hpp"

struct GLFWwindow;

namespace bgui {
    struct vulkan_backend_data {
        VkInstance instance = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        VkQueue graphics_queue = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE; // compatibility spelling
        VkQueue present_queue = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkSurfaceFormatKHR surface_format{};
        VkExtent2D swapchain_extent{};
        uint32_t graphics_family = 0;
        GLFWwindow* window = nullptr;
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    };

    extern vulkan_backend_data vk;
    void vulkan_render(bgui::draw_data*);
    void create_vk_instance();
    VKAPI_ATTR VkBool32 VKAPI_CALL bgui_vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);

    //void vulkan_clear_texture_cache();
    void set_up_vulkan();
    // Enables linear font-atlas filtering when true, or nearest filtering when false.
    void set_font_antialiasing(bool enabled);
#ifdef BGUI_USE_GLFW
    void set_up_vulkan(GLFWwindow* window);
#endif
    void shutdown_vulkan();
    //GLuint get_quad_vao();
    //GLuint vulkan_get_texture(const bgui::texture& tex);
} // namespace bgui