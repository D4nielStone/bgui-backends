#include "bgui_backend_vulkan.hpp"

#ifdef BGUI_USE_GLFW
#include <GLFW/glfw3.h>
#endif

#include "embedded_spirv.hpp"
#include "os/os.hpp"
#include "os/style_manager.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {
constexpr uint32_t kFramesInFlight = 2;
constexpr VkDeviceSize kPushConstantSize = 160;

struct PushConstants {
    float projection[16];
    float rect[4];
    float uv_min[2];
    float uv_max[2];
    float bg_color[4];
    float border_color[4];
    float text_color[4];
    float border_radius = 0.f;
    float border_size = 0.f;
    int bordered = 0;
    int use_tex = 0;
};
static_assert(sizeof(PushConstants) == kPushConstantSize);

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
};

struct Frame {
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkSemaphore render_finished = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
};

static std::unordered_map<std::string, Texture> textures;
static VkSampler sampler = VK_NULL_HANDLE;
static VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
static VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
static VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
static VkPipeline pipeline = VK_NULL_HANDLE;
static VkRenderPass render_pass = VK_NULL_HANDLE;
static VkCommandPool command_pool = VK_NULL_HANDLE;
static std::vector<VkImage> swapchain_images;
static std::vector<VkImageView> swapchain_views;
static std::vector<VkFramebuffer> framebuffers;
static std::vector<Frame> frames(kFramesInFlight);
static uint32_t frame_index = 0;
static bool initialized = false;
static bool framebuffer_resized = false;
static bool s_font_antialiasing = true;

std::string texture_key(const bgui::texture& t) {
    std::string key = t.m_path + "|" + std::to_string(t.m_id) + "|" +
        std::to_string(t.m_size[0]) + "x" + std::to_string(t.m_size[1]) + "|" +
        std::to_string(t.m_buffer.size()) + "|" + (t.m_use_red_channel ? "r" : "c");
    if (!t.m_buffer.empty()) key.append(reinterpret_cast<const char*>(t.m_buffer.data()), t.m_buffer.size());
    return key;
}

void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(what) + ": " + std::to_string(result));
}

uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(bgui::vk.physical_device, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mem.memoryTypes[i].propertyFlags & properties) == properties) return i;
    throw std::runtime_error("No compatible Vulkan memory type");
}

void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                   VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size; info.usage = usage; info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(bgui::vk.device, &info, nullptr, &buffer), "vkCreateBuffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(bgui::vk.device, buffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size; alloc.memoryTypeIndex = memory_type(req.memoryTypeBits, props);
    check(vkAllocateMemory(bgui::vk.device, &alloc, nullptr, &memory), "vkAllocateMemory");
    check(vkBindBufferMemory(bgui::vk.device, buffer, memory, 0), "vkBindBufferMemory");
}

VkCommandBuffer begin_one_time() {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = command_pool; alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; alloc.commandBufferCount = 1;
    VkCommandBuffer cmd; check(vkAllocateCommandBuffers(bgui::vk.device, &alloc, &cmd), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");
    return cmd;
}

void end_one_time(VkCommandBuffer cmd) {
    check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1; submit.pCommandBuffers = &cmd;
    check(vkQueueSubmit(bgui::vk.graphics_queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
    check(vkQueueWaitIdle(bgui::vk.graphics_queue), "vkQueueWaitIdle");
    vkFreeCommandBuffers(bgui::vk.device, command_pool, 1, &cmd);
}

void transition(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = old_layout; barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image; barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1; barrier.subresourceRange.layerCount = 1;
    VkPipelineStageFlags src = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        dst = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    } else {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkShaderModule shader(const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size; info.pCode = code;
    VkShaderModule result; check(vkCreateShaderModule(bgui::vk.device, &info, nullptr, &result), "vkCreateShaderModule");
    return result;
}

void create_texture(const bgui::texture& source, Texture& out) {
    uint32_t width = source.m_size[0] ? static_cast<uint32_t>(source.m_size[0]) : 1;
    uint32_t height = source.m_size[1] ? static_cast<uint32_t>(source.m_size[1]) : 1;
    const uint32_t channels = source.m_use_red_channel ? 1 : (source.m_has_alpha ? 4 : 3);
    std::vector<unsigned char> pixels;
    if (source.m_buffer.size() >= static_cast<size_t>(width) * height * channels)
        pixels = source.m_buffer;
    else
        pixels = {255, 255, 255, 255};
    if (channels == 3) {
        std::vector<unsigned char> rgba(static_cast<size_t>(width) * height * 4, 255);
        for (size_t i = 0, o = 0; i + 2 < pixels.size() && o + 3 < rgba.size(); i += 3, o += 4)
            std::copy_n(pixels.data() + i, 3, rgba.data() + o);
        pixels.swap(rgba);
    } else if (channels == 1) {
        std::vector<unsigned char> red(static_cast<size_t>(width) * height, 0);
        std::copy_n(pixels.data(), std::min(red.size(), pixels.size()), red.data());
        pixels.swap(red);
    }
    VkFormat format = channels == 1 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8B8A8_UNORM;
    VkDeviceSize size = pixels.size();
    VkBuffer staging; VkDeviceMemory staging_memory;
    create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  staging, staging_memory);
    void* mapped; vkMapMemory(bgui::vk.device, staging_memory, 0, size, 0, &mapped);
    std::memcpy(mapped, pixels.data(), size); vkUnmapMemory(bgui::vk.device, staging_memory);

    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = VK_IMAGE_TYPE_2D; image_info.format = format;
    image_info.extent = {width, height, 1}; image_info.mipLevels = 1; image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT; image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    check(vkCreateImage(bgui::vk.device, &image_info, nullptr, &out.image), "vkCreateImage");
    VkMemoryRequirements req{}; vkGetImageMemoryRequirements(bgui::vk.device, out.image, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size; alloc.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(bgui::vk.device, &alloc, nullptr, &out.memory), "vkAllocateMemory");
    check(vkBindImageMemory(bgui::vk.device, out.image, out.memory, 0), "vkBindImageMemory");

    VkCommandBuffer cmd = begin_one_time();
    transition(cmd, out.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{}; copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1; copy.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, staging, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    transition(cmd, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    end_one_time(cmd);
    vkDestroyBuffer(bgui::vk.device, staging, nullptr); vkFreeMemory(bgui::vk.device, staging_memory, nullptr);

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = out.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; view.subresourceRange.levelCount = 1; view.subresourceRange.layerCount = 1;
    check(vkCreateImageView(bgui::vk.device, &view, nullptr, &out.view), "vkCreateImageView");
    VkDescriptorSetAllocateInfo set{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    set.descriptorPool = descriptor_pool; set.descriptorSetCount = 1; set.pSetLayouts = &descriptor_layout;
    check(vkAllocateDescriptorSets(bgui::vk.device, &set, &out.descriptor), "vkAllocateDescriptorSets");
    VkDescriptorImageInfo image_info_desc{sampler, out.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = out.descriptor; write.dstBinding = 0; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; write.pImageInfo = &image_info_desc;
    vkUpdateDescriptorSets(bgui::vk.device, 1, &write, 0, nullptr);
}

void destroy_texture(Texture& texture) {
    if (texture.view) vkDestroyImageView(bgui::vk.device, texture.view, nullptr);
    if (texture.image) vkDestroyImage(bgui::vk.device, texture.image, nullptr);
    if (texture.memory) vkFreeMemory(bgui::vk.device, texture.memory, nullptr);
}

void create_sampler() {
    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    const VkFilter filter = s_font_antialiasing ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    sampler_info.magFilter = filter;
    sampler_info.minFilter = filter;
    sampler_info.mipmapMode = s_font_antialiasing
        ? VK_SAMPLER_MIPMAP_MODE_LINEAR
        : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = sampler_info.addressModeV =
        sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = 1.f;
    check(vkCreateSampler(bgui::vk.device, &sampler_info, nullptr, &sampler),
          "vkCreateSampler");
}

void cleanup_swapchain() {
    for (auto f : framebuffers) vkDestroyFramebuffer(bgui::vk.device, f, nullptr);
    framebuffers.clear();
    for (auto v : swapchain_views) vkDestroyImageView(bgui::vk.device, v, nullptr);
    swapchain_views.clear();
    if (bgui::vk.swapchain) vkDestroySwapchainKHR(bgui::vk.device, bgui::vk.swapchain, nullptr);
    bgui::vk.swapchain = VK_NULL_HANDLE;
}

void create_swapchain() {
#ifdef BGUI_USE_GLFW
    int w = 0, h = 0; glfwGetFramebufferSize(bgui::vk.window, &w, &h);
    if (w == 0 || h == 0) return;
#endif
    VkSurfaceCapabilitiesKHR caps{}; vkGetPhysicalDeviceSurfaceCapabilitiesKHR(bgui::vk.physical_device, bgui::vk.surface, &caps);
    uint32_t format_count = 0; vkGetPhysicalDeviceSurfaceFormatsKHR(bgui::vk.physical_device, bgui::vk.surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count); vkGetPhysicalDeviceSurfaceFormatsKHR(bgui::vk.physical_device, bgui::vk.surface, &format_count, formats.data());
    bgui::vk.surface_format = formats[0];
    for (auto f : formats) if (f.format == VK_FORMAT_B8G8R8A8_UNORM) bgui::vk.surface_format = f;
    uint32_t present_count = 0; vkGetPhysicalDeviceSurfacePresentModesKHR(bgui::vk.physical_device, bgui::vk.surface, &present_count, nullptr);
    std::vector<VkPresentModeKHR> presents(present_count); vkGetPhysicalDeviceSurfacePresentModesKHR(bgui::vk.physical_device, bgui::vk.surface, &present_count, presents.data());
    VkPresentModeKHR present = VK_PRESENT_MODE_FIFO_KHR;
    for (auto p : presents) if (p == VK_PRESENT_MODE_MAILBOX_KHR) present = p;
    VkExtent2D extent = caps.currentExtent;
#ifdef BGUI_USE_GLFW
    if (extent.width == std::numeric_limits<uint32_t>::max())
        extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
#endif
    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = bgui::vk.surface; info.minImageCount = count; info.imageFormat = bgui::vk.surface_format.format;
    info.imageColorSpace = bgui::vk.surface_format.colorSpace; info.imageExtent = extent; info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform; info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = present; info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(bgui::vk.device, &info, nullptr, &bgui::vk.swapchain), "vkCreateSwapchainKHR");
    vkGetSwapchainImagesKHR(bgui::vk.device, bgui::vk.swapchain, &count, nullptr);
    swapchain_images.resize(count); vkGetSwapchainImagesKHR(bgui::vk.device, bgui::vk.swapchain, &count, swapchain_images.data());
    bgui::vk.swapchain_extent = extent;
    swapchain_views.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = swapchain_images[i]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = bgui::vk.surface_format.format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; view.subresourceRange.levelCount = 1; view.subresourceRange.layerCount = 1;
        check(vkCreateImageView(bgui::vk.device, &view, nullptr, &swapchain_views[i]), "vkCreateImageView");
    }
    framebuffers.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageView attachments[] = {swapchain_views[i]};
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = render_pass; fb.attachmentCount = 1; fb.pAttachments = attachments;
        fb.width = extent.width; fb.height = extent.height; fb.layers = 1;
        check(vkCreateFramebuffer(bgui::vk.device, &fb, nullptr, &framebuffers[i]), "vkCreateFramebuffer");
    }
}

void recreate_swapchain() {
    vkDeviceWaitIdle(bgui::vk.device);
    cleanup_swapchain();
    create_swapchain();
    framebuffer_resized = false;
}

void create_pipeline() {
    VkShaderModule vert = shader(bgui::spirv::vertex, sizeof(bgui::spirv::vertex));
    VkShaderModule frag = shader(bgui::spirv::fragment, sizeof(bgui::spirv::fragment));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1; viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1.f; raster.cullMode = VK_CULL_MODE_NONE; raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE; blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD; blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1; blending.pAttachments = &blend;
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamic_states;
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, kPushConstantSize};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1; layout.pSetLayouts = &descriptor_layout; layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(bgui::vk.device, &layout, nullptr, &pipeline_layout), "vkCreatePipelineLayout");
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &vertex_input; info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport; info.pRasterizationState = &raster; info.pMultisampleState = &multisample;
    info.pColorBlendState = &blending; info.pDynamicState = &dynamic; info.layout = pipeline_layout; info.renderPass = render_pass; info.subpass = 0;
    check(vkCreateGraphicsPipelines(bgui::vk.device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(bgui::vk.device, vert, nullptr); vkDestroyShaderModule(bgui::vk.device, frag, nullptr);
}
} // namespace

namespace bgui {
vulkan_backend_data vk{};

VKAPI_ATTR VkBool32 VKAPI_CALL bgui_vk_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    std::cerr << (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "[Vulkan error] " : "[Vulkan] ")
              << data->pMessage << '\n';
    return VK_FALSE;
}

void create_vk_instance() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "cpp-bgui"; app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "cpp-bgui"; app.engineVersion = VK_MAKE_VERSION(1, 0, 0); app.apiVersion = VK_API_VERSION_1_0;
    std::vector<const char*> extensions;
#ifdef BGUI_USE_GLFW
    uint32_t count = 0; const char** required = glfwGetRequiredInstanceExtensions(&count);
    if (!required) throw std::runtime_error("GLFW has no Vulkan extensions");
    extensions.assign(required, required + count);
#endif
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app; info.enabledExtensionCount = static_cast<uint32_t>(extensions.size()); info.ppEnabledExtensionNames = extensions.data();
    check(vkCreateInstance(&info, nullptr, &vk.instance), "vkCreateInstance");
}

void set_up_vulkan() { create_vk_instance(); }

#ifdef BGUI_USE_GLFW
void set_up_vulkan(GLFWwindow* window) {
    if (!window) throw std::invalid_argument("Cannot set up Vulkan with a null GLFW window");
    vk.window = window;
    create_vk_instance();
    check(glfwCreateWindowSurface(vk.instance, window, nullptr, &vk.surface), "glfwCreateWindowSurface");
    uint32_t count = 0; vkEnumeratePhysicalDevices(vk.instance, &count, nullptr);
    if (!count) throw std::runtime_error("No Vulkan physical device found");
    std::vector<VkPhysicalDevice> devices(count); vkEnumeratePhysicalDevices(vk.instance, &count, devices.data());
    for (auto d : devices) {
        uint32_t families = 0; vkGetPhysicalDeviceQueueFamilyProperties(d, &families, nullptr);
        std::vector<VkQueueFamilyProperties> props(families); vkGetPhysicalDeviceQueueFamilyProperties(d, &families, props.data());
        for (uint32_t i = 0; i < families; ++i) {
            VkBool32 present = VK_FALSE; vkGetPhysicalDeviceSurfaceSupportKHR(d, i, vk.surface, &present);
            if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { vk.physical_device = d; vk.graphics_family = i; break; }
        }
        if (vk.physical_device) break;
    }
    if (!vk.physical_device) throw std::runtime_error("No Vulkan graphics/present queue found");
    float priority = 1.f;
    VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue.queueFamilyIndex = vk.graphics_family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
    const char* swapchain_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue; device.enabledExtensionCount = 1; device.ppEnabledExtensionNames = &swapchain_ext;
    check(vkCreateDevice(vk.physical_device, &device, nullptr, &vk.device), "vkCreateDevice");
    vkGetDeviceQueue(vk.device, vk.graphics_family, 0, &vk.graphics_queue);
    vk.graphicsQueue = vk.graphics_queue;
    vk.present_queue = vk.graphics_queue;
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pool.queueFamilyIndex = vk.graphics_family;
    check(vkCreateCommandPool(vk.device, &pool, nullptr, &command_pool), "vkCreateCommandPool");
    uint32_t surface_format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, vk.surface, &surface_format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk.physical_device, vk.surface, &surface_format_count, surface_formats.data());
    vk.surface_format = surface_formats.front();
    for (auto format : surface_formats)
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM) vk.surface_format = format;
    VkAttachmentDescription attachment{}; attachment.format = vk.surface_format.format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; rp.attachmentCount = 1; rp.pAttachments = &attachment; rp.subpassCount = 1; rp.pSubpasses = &sub;
    check(vkCreateRenderPass(vk.device, &rp, nullptr, &render_pass), "vkCreateRenderPass");
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo set_layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; set_layout.bindingCount = 1; set_layout.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(vk.device, &set_layout, nullptr, &descriptor_layout), "vkCreateDescriptorSetLayout");
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256};
    VkDescriptorPoolCreateInfo desc_pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; desc_pool.maxSets = 256; desc_pool.poolSizeCount = 1; desc_pool.pPoolSizes = &pool_size;
    check(vkCreateDescriptorPool(vk.device, &desc_pool, nullptr, &descriptor_pool), "vkCreateDescriptorPool");
    create_sampler();
    create_pipeline();
    create_swapchain();
    VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commands.commandPool = command_pool; commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commands.commandBufferCount = kFramesInFlight;
    std::array<VkCommandBuffer, kFramesInFlight> command_buffers{}; check(vkAllocateCommandBuffers(vk.device, &commands, command_buffers.data()), "vkAllocateCommandBuffers");
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frames[i].command = command_buffers[i];
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(vk.device, &sem, nullptr, &frames[i].image_available), "vkCreateSemaphore");
        check(vkCreateSemaphore(vk.device, &sem, nullptr, &frames[i].render_finished), "vkCreateSemaphore");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(vk.device, &fence, nullptr, &frames[i].fence), "vkCreateFence");
    }
    initialized = true;
}
#endif

void set_font_antialiasing(bool enabled) {
    if (s_font_antialiasing == enabled)
        return;

    s_font_antialiasing = enabled;
    if (!initialized || !bgui::vk.device)
        return;

    vkDeviceWaitIdle(bgui::vk.device);
    for (auto& entry : textures)
        destroy_texture(entry.second);
    textures.clear();
    check(vkResetDescriptorPool(bgui::vk.device, descriptor_pool, 0),
          "vkResetDescriptorPool");
    vkDestroySampler(bgui::vk.device, sampler, nullptr);
    sampler = VK_NULL_HANDLE;
    create_sampler();
}

void vulkan_render(draw_data* data) {
    if (!data || !initialized || framebuffers.empty()) return;
#ifdef BGUI_USE_GLFW
    int framebuffer_width = 0, framebuffer_height = 0;
    glfwGetFramebufferSize(vk.window, &framebuffer_width, &framebuffer_height);
    if (framebuffer_width == 0 || framebuffer_height == 0) return;
    if (static_cast<uint32_t>(framebuffer_width) != vk.swapchain_extent.width ||
        static_cast<uint32_t>(framebuffer_height) != vk.swapchain_extent.height) {
        recreate_swapchain();
        if (framebuffers.empty()) return;
    }
#endif
    Frame& frame = frames[frame_index];
    vkWaitForFences(vk.device, 1, &frame.fence, VK_TRUE, UINT64_MAX);
    uint32_t image = 0;
    VkResult result = vkAcquireNextImageKHR(vk.device, vk.swapchain, UINT64_MAX, frame.image_available, VK_NULL_HANDLE, &image);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { recreate_swapchain(); return; }
    if (result == VK_SUBOPTIMAL_KHR) framebuffer_resized = true;
    check(result, "vkAcquireNextImageKHR");
    vkResetFences(vk.device, 1, &frame.fence); vkResetCommandBuffer(frame.command, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; check(vkBeginCommandBuffer(frame.command, &begin), "vkBeginCommandBuffer");
    VkClearValue clear{};
    const auto& global_visual = bgui::style_manager::get_instance().get_global().visual;
    if (global_visual.background.normal) {
        const auto& color = *global_visual.background.normal;
        clear.color = {{color.r, color.g, color.b, color.a}};
    } else {
        clear.color = {{0.f, 0.f, 0.f, 0.f}};
    }
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; pass.renderPass = render_pass; pass.framebuffer = framebuffers[image];
    pass.renderArea.extent = vk.swapchain_extent; pass.clearValueCount = 1; pass.pClearValues = &clear;
    vkCmdBeginRenderPass(frame.command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0, 0, static_cast<float>(vk.swapchain_extent.width), static_cast<float>(vk.swapchain_extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, vk.swapchain_extent}; vkCmdSetViewport(frame.command, 0, 1, &viewport); vkCmdSetScissor(frame.command, 0, 1, &scissor);
    vkCmdBindPipeline(frame.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    while (!data->m_quad_requires.empty()) {
        auto call = data->m_quad_requires.front(); data->m_quad_requires.pop();
        PushConstants pc{};
        auto projection = get_projection();
        std::memcpy(pc.projection, projection.data(), sizeof(pc.projection));
        // get_projection() uses OpenGL's bottom-left clip-space convention.
        // Vulkan's default viewport has the opposite Y direction.
        pc.projection[1] = -pc.projection[1];
        pc.projection[5] = -pc.projection[5];
        pc.projection[9] = -pc.projection[9];
        pc.projection[13] = -pc.projection[13];
        std::memcpy(pc.rect, call.m_rect.v.data(), sizeof(pc.rect));
        std::memcpy(pc.uv_min, call.m_uv_min.v.data(), sizeof(pc.uv_min));
        std::memcpy(pc.uv_max, call.m_uv_max.v.data(), sizeof(pc.uv_max));
        if (auto it = call.m_material.m_properties.find("bg_color"); it != call.m_material.m_properties.end() && it->second.m_type == 2) std::memcpy(pc.bg_color, it->second.m_value.m_vec4.v.data(), sizeof(pc.bg_color));
        if (auto it = call.m_material.m_properties.find("border_color"); it != call.m_material.m_properties.end() && it->second.m_type == 2) std::memcpy(pc.border_color, it->second.m_value.m_vec4.v.data(), sizeof(pc.border_color));
        if (auto it = call.m_material.m_properties.find("text_color"); it != call.m_material.m_properties.end() && it->second.m_type == 2) std::memcpy(pc.text_color, it->second.m_value.m_vec4.v.data(), sizeof(pc.text_color));
        if (auto it = call.m_material.m_properties.find("border_radius"); it != call.m_material.m_properties.end() && it->second.m_type == 5) pc.border_radius = it->second.m_value.m_float;
        if (auto it = call.m_material.m_properties.find("border_size"); it != call.m_material.m_properties.end() && it->second.m_type == 5) pc.border_size = it->second.m_value.m_float;
        if (auto it = call.m_material.m_properties.find("bordered"); it != call.m_material.m_properties.end() && it->second.m_type == 6) pc.bordered = it->second.m_value.m_int != 0;
        pc.use_tex = call.m_material.m_use_tex ? 1 : 0;
        Texture* tex = nullptr;
        if (call.m_material.m_use_tex) {
            auto key = texture_key(call.m_material.m_texture);
            auto it = textures.find(key);
            if (it == textures.end()) it = textures.emplace(key, Texture{}).first, create_texture(call.m_material.m_texture, it->second);
            tex = &it->second;
        } else {
            static bgui::texture white; white.m_size = {1, 1}; white.m_buffer = {255, 255, 255, 255}; white.m_has_alpha = true;
            auto key = texture_key(white); auto it = textures.find(key);
            if (it == textures.end()) it = textures.emplace(key, Texture{}).first, create_texture(white, it->second);
            tex = &it->second;
        }
        vkCmdBindDescriptorSets(frame.command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout, 0, 1, &tex->descriptor, 0, nullptr);
        vkCmdPushConstants(frame.command, pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
        vkCmdDraw(frame.command, static_cast<uint32_t>(call.m_count > 0 ? call.m_count : 6), 1, 0, 0);
    }
    vkCmdEndRenderPass(frame.command); check(vkEndCommandBuffer(frame.command), "vkEndCommandBuffer");
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &frame.image_available; submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1; submit.pCommandBuffers = &frame.command; submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &frame.render_finished;
    check(vkQueueSubmit(vk.graphics_queue, 1, &submit, frame.fence), "vkQueueSubmit");
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; present.waitSemaphoreCount = 1; present.pWaitSemaphores = &frame.render_finished;
    present.swapchainCount = 1; present.pSwapchains = &vk.swapchain; present.pImageIndices = &image;
    result = vkQueuePresentKHR(vk.present_queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebuffer_resized) recreate_swapchain();
    else check(result, "vkQueuePresentKHR");
    frame_index = (frame_index + 1) % kFramesInFlight;
}

void shutdown_vulkan() {
    if (!vk.instance) return;
    if (vk.device) {
        vkDeviceWaitIdle(vk.device);
        for (auto& t : textures) destroy_texture(t.second);
        textures.clear();
        for (auto& f : frames) { if (f.fence) vkDestroyFence(vk.device, f.fence, nullptr); if (f.image_available) vkDestroySemaphore(vk.device, f.image_available, nullptr); if (f.render_finished) vkDestroySemaphore(vk.device, f.render_finished, nullptr); }
        cleanup_swapchain();
        if (sampler) vkDestroySampler(vk.device, sampler, nullptr);
        if (pipeline) vkDestroyPipeline(vk.device, pipeline, nullptr);
        if (pipeline_layout) vkDestroyPipelineLayout(vk.device, pipeline_layout, nullptr);
        if (descriptor_pool) vkDestroyDescriptorPool(vk.device, descriptor_pool, nullptr);
        if (descriptor_layout) vkDestroyDescriptorSetLayout(vk.device, descriptor_layout, nullptr);
        if (render_pass) vkDestroyRenderPass(vk.device, render_pass, nullptr);
        if (command_pool) vkDestroyCommandPool(vk.device, command_pool, nullptr);
        vkDestroyDevice(vk.device, nullptr);
    }
    if (vk.surface) vkDestroySurfaceKHR(vk.instance, vk.surface, nullptr);
    if (vk.instance) vkDestroyInstance(vk.instance, nullptr);
    vk = {}; initialized = false;
}
} // namespace bgui
