#include "bgui_backend_vulkan.hpp"
#include <stdexcept>
#include <iostream>
#include <vector>
#include <cstring>
#ifdef BGUI_USE_GLFW
#include <GLFW/glfw3.h>
#endif

// Function to create debug messenger
VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pMessenger) 
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

// Debug callback function
VKAPI_ATTR VkBool32 VKAPI_CALL bgui::bgui_vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageTypes,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        std::cerr << "[VULKAN ERROR] " << pCallbackData->pMessage << std::endl;
    else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::cerr << "[VULKAN WARNING] " << pCallbackData->pMessage << std::endl;
    else
        std::cout << "[VULKAN INFO] " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}

// Creates Vulkan instance + debug messenger
void bgui::create_vk_instance() {
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "BGUI Application";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // Creates DebugMessengerCreateInfo for pNext
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = bgui::bgui_vk_debug_callback;
    debugCreateInfo.pUserData = nullptr;

#ifdef BGUI_USE_GLFW
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions || glfwExtensionCount == 0) {
        throw std::runtime_error(
            "GLFW did not provide the Vulkan instance extensions required to create a surface."
        );
    }
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
#endif

    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    bool enableValidationLayers = false;
    for (const auto& layer : availableLayers) {
        if (std::strcmp(layer.layerName, validationLayer) == 0) {
            enableValidationLayers = true;
            break;
        }
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &validationLayer;
#ifdef BGUI_USE_GLFW
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
        createInfo.pNext = &debugCreateInfo;
    }
#ifdef BGUI_USE_GLFW
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
#endif

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &vk.instance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            "VK bgui::failed to create instance: " + std::to_string(result)
        );
    }

    // Creates the actual messenger
    vk.debugMessenger = VK_NULL_HANDLE;
    if (enableValidationLayers)
        CreateDebugUtilsMessengerEXT(vk.instance, &debugCreateInfo, nullptr, &vk.debugMessenger);
}

void bgui::shutdown_vulkan() {
    if (vk.debugMessenger != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(vk.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(vk.instance, vk.debugMessenger, nullptr);
    }

    if (vk.surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(vk.instance, vk.surface, nullptr);

    if (vk.instance != VK_NULL_HANDLE)
        vkDestroyInstance(vk.instance, nullptr);
}

void bgui::set_up_vulkan() {
    bgui::create_vk_instance();
}

void bgui::vulkan_render(bgui::draw_data* draw_data) {
    if (!draw_data)
        return;

    // The Vulkan renderer is not implemented yet, but every backend must
    // consume the frame's draw queue so the next update starts clean.
    while (!draw_data->m_quad_requires.empty())
        draw_data->m_quad_requires.pop();
}