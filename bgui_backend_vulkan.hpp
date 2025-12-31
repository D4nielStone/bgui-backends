#pragma once
#include <vulkan/vulkan.h>
#include "utils/draw.hpp"

namespace bgui {
    struct vulkan_backend_data {
        VkInstance instance = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    };

    static vulkan_backend_data vk;
    void vulkan_render(bgui::draw_data*);
    void create_vk_instance();
    VKAPI_ATTR VkBool32 VKAPI_CALL bgui_vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData);

    //void vulkan_clear_texture_cache();
    void set_up_vulkan();
    void shutdown_vulkan();
    //GLuint get_quad_vao();
    //GLuint vulkan_get_texture(const bgui::texture& tex);
} // namespace bgui