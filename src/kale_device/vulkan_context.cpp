// Kale 设备抽象层 - Vulkan 基础上下文实现

#include <kale_device/vulkan_context.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace kale_device {

namespace {

constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

bool CheckValidationLayerSupport() {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const auto& layer : availableLayers) {
        if (strcmp(layer.layerName, kValidationLayerName) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

VulkanContext::~VulkanContext() {
    Shutdown();
}

bool VulkanContext::Initialize(const VulkanConfig& config) {
    lastError_.clear();
    if (config.windowHandle == nullptr || config.width == 0 || config.height == 0) {
        lastError_ = "VulkanConfig: windowHandle, width, height required";
        return false;
    }

    if (!CreateInstance(config)) return false;
    if (!CreateSurface(config)) return false;   // Surface 需在选 Physical Device 前创建（用于 present 支持查询）
    if (!SelectPhysicalDevice()) return false;
    if (!CreateLogicalDevice()) return false;
    if (!CreateSwapchain(config)) return false;

    return true;
}

void VulkanContext::Shutdown() {
    if (device_ != nullptr) {
        vkDeviceWaitIdle(device_);
    }

    DestroyTriangleRendering();

    if (swapchain_ != nullptr) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = nullptr;
    }
    swapchainImages_.clear();
    swapchainWidth_ = swapchainHeight_ = 0;

    if (surface_ != nullptr) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = nullptr;
    }

    if (device_ != nullptr) {
        vkDestroyDevice(device_, nullptr);
        device_ = nullptr;
    }
    physicalDevice_ = nullptr;
    graphicsQueue_ = nullptr;
    presentQueue_ = nullptr;

    if (instance_ != nullptr) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = nullptr;
    }
    validationEnabled_ = false;
}

bool VulkanContext::CreateInstance(const VulkanConfig& config) {
    validationEnabled_ = config.enableValidation;
    if (validationEnabled_ && !CheckValidationLayerSupport()) {
        lastError_ = "Validation layer requested but VK_LAYER_KHRONOS_validation not available";
        return false;
    }

    // SDL 要求的扩展（SDL3 仅需传入 count 指针，返回只读数组，由 SDL 持有）
    uint32_t sdlExtensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    if (!sdlExtensions || sdlExtensionCount == 0) {
        lastError_ = std::string("SDL_Vulkan_GetInstanceExtensions: ") + (SDL_GetError() ? SDL_GetError() : "no extensions");
        return false;
    }
    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);

    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Kale";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Kale";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (validationEnabled_) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &kValidationLayerName;
    } else {
        createInfo.enabledLayerCount = 0;
    }

    VkResult err = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (err != VK_SUCCESS) {
        lastError_ = "vkCreateInstance failed with " + std::to_string(err);
        return false;
    }
    return true;
}

bool VulkanContext::SelectPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (deviceCount == 0) {
        lastError_ = "No Vulkan physical devices found";
        return false;
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    for (VkPhysicalDevice dev : devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; ++i) {
            if (!(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface_, &presentSupport);
            if (presentSupport == VK_TRUE) {
                physicalDevice_ = dev;
                graphicsQueueFamilyIndex_ = i;
                return true;
            }
        }
    }

    lastError_ = "No suitable physical device (graphics + present)";
    return false;
}

bool VulkanContext::CreateSurface(const VulkanConfig& config) {
    SDL_Window* window = static_cast<SDL_Window*>(config.windowHandle);
    if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
        lastError_ = std::string("SDL_Vulkan_CreateSurface: ") + SDL_GetError();
        return false;
    }
    return true;
}

bool VulkanContext::CreateLogicalDevice() {
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex_;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    const char* swapchainExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = &swapchainExtension;
    createInfo.pEnabledFeatures = nullptr;

    VkResult err = vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_);
    if (err != VK_SUCCESS) {
        lastError_ = "vkCreateDevice failed with " + std::to_string(err);
        return false;
    }

    vkGetDeviceQueue(device_, graphicsQueueFamilyIndex_, 0, &graphicsQueue_);
    presentQueue_ = graphicsQueue_;  // 同一 queue family 时共用
    return true;
}


bool VulkanContext::CreateSwapchain(const VulkanConfig& config) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &formatCount, formats.data());

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &presentModeCount, presentModes.data());

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = f;
            break;
        }
    }

    VkPresentModeKHR chosenPresentMode = config.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
    bool found = false;
    for (const auto& m : presentModes) {
        if (m == chosenPresentMode) {
            found = true;
            break;
        }
    }
    if (!found) {
        chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    }

    uint32_t imageCount = config.backBufferCount;
    if (imageCount < caps.minImageCount) imageCount = caps.minImageCount;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    vsync_ = config.vsync;
    backBufferCount_ = config.backBufferCount;
    swapchainFormat_ = chosenFormat.format;
    swapchainWidth_ = config.width;
    swapchainHeight_ = config.height;
    if (swapchainWidth_ < caps.minImageExtent.width) swapchainWidth_ = caps.minImageExtent.width;
    if (swapchainWidth_ > caps.maxImageExtent.width) swapchainWidth_ = caps.maxImageExtent.width;
    if (swapchainHeight_ < caps.minImageExtent.height) swapchainHeight_ = caps.minImageExtent.height;
    if (swapchainHeight_ > caps.maxImageExtent.height) swapchainHeight_ = caps.maxImageExtent.height;

    VkSwapchainCreateInfoKHR swapInfo = {};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = surface_;
    swapInfo.minImageCount = imageCount;
    swapInfo.imageFormat = chosenFormat.format;
    swapInfo.imageColorSpace = chosenFormat.colorSpace;
    swapInfo.imageExtent = { swapchainWidth_, swapchainHeight_ };
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.preTransform = caps.currentTransform;
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode = chosenPresentMode;
    swapInfo.clipped = VK_TRUE;

    VkResult err = vkCreateSwapchainKHR(device_, &swapInfo, nullptr, &swapchain_);
    if (err != VK_SUCCESS) {
        lastError_ = "vkCreateSwapchainKHR failed with " + std::to_string(err);
        return false;
    }

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    swapchainImages_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchainImages_.data());

    return true;
}

bool VulkanContext::RecreateSwapchain(uint32_t width, uint32_t height) {
    if (device_ == nullptr || surface_ == nullptr) return false;
    vkDeviceWaitIdle(device_);

    for (auto fb : framebuffers_) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device_, fb, nullptr);
    }
    framebuffers_.clear();

    for (auto iv : swapchainImageViews_) {
        if (iv != VK_NULL_HANDLE) vkDestroyImageView(device_, iv, nullptr);
    }
    swapchainImageViews_.clear();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    swapchainImages_.clear();

    VulkanConfig config;
    config.windowHandle = nullptr;
    config.width = width;
    config.height = height;
    config.vsync = vsync_;
    config.backBufferCount = backBufferCount_;
    config.enableValidation = validationEnabled_;
    if (!CreateSwapchain(config)) return false;

    if (!CreateImageViews()) return false;
    if (renderPass_ != VK_NULL_HANDLE && !CreateFramebuffers()) return false;
    return true;
}

VkImage VulkanContext::GetSwapchainImage(uint32_t index) const {
    if (index >= swapchainImages_.size()) return nullptr;
    return swapchainImages_[index];
}

VkFramebuffer VulkanContext::GetFramebuffer(uint32_t imageIndex) const {
    if (imageIndex >= framebuffers_.size()) return nullptr;
    return framebuffers_[imageIndex];
}

// -----------------------------------------------------------------------------
// 简单三角形渲染（phase1-1.3）
// -----------------------------------------------------------------------------

bool VulkanContext::CreateTriangleRendering(const char* shaderDir) {
    lastError_.clear();
    if (!CreateTriangleRenderingImpl(shaderDir)) {
        DestroyTriangleRendering();
        return false;
    }
    return true;
}

void VulkanContext::DestroyTriangleRendering() {
    if (device_ == nullptr) return;

    vkDeviceWaitIdle(device_);

    for (size_t i = 0; i < inFlightFences_.size(); ++i) {
        vkDestroyFence(device_, inFlightFences_[i], nullptr);
    }
    inFlightFences_.clear();
    for (size_t i = 0; i < renderFinishedSemaphores_.size(); ++i) {
        vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
    }
    renderFinishedSemaphores_.clear();
    for (size_t i = 0; i < imageAvailableSemaphores_.size(); ++i) {
        vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
    }
    imageAvailableSemaphores_.clear();

    if (commandPool_ != nullptr) {
        vkDestroyCommandPool(device_, commandPool_, nullptr);
        commandPool_ = nullptr;
    }
    commandBuffers_.clear();

    if (vertexBuffer_ != nullptr) {
        vkDestroyBuffer(device_, vertexBuffer_, nullptr);
        vertexBuffer_ = nullptr;
    }
    if (vertexBufferMemory_ != nullptr) {
        vkFreeMemory(device_, vertexBufferMemory_, nullptr);
        vertexBufferMemory_ = nullptr;
    }

    if (graphicsPipeline_ != nullptr) {
        vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
        graphicsPipeline_ = nullptr;
    }
    if (pipelineLayout_ != nullptr) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        pipelineLayout_ = nullptr;
    }
    if (vertShaderModule_ != nullptr) {
        vkDestroyShaderModule(device_, vertShaderModule_, nullptr);
        vertShaderModule_ = nullptr;
    }
    if (fragShaderModule_ != nullptr) {
        vkDestroyShaderModule(device_, fragShaderModule_, nullptr);
        fragShaderModule_ = nullptr;
    }

    for (auto fb : framebuffers_) {
        vkDestroyFramebuffer(device_, fb, nullptr);
    }
    framebuffers_.clear();

    if (renderPass_ != nullptr) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = nullptr;
    }

    for (auto iv : swapchainImageViews_) {
        vkDestroyImageView(device_, iv, nullptr);
    }
    swapchainImageViews_.clear();
}

bool VulkanContext::CreateTriangleRenderingImpl(const char* shaderDir) {
    if (!CreateImageViews()) return false;
    if (!CreateRenderPass()) return false;
    if (!CreateGraphicsPipeline(shaderDir)) return false;
    if (!CreateVertexBuffer()) return false;
    if (!CreateFramebuffers()) return false;
    if (!CreateCommandPoolAndBuffers()) return false;
    if (!CreateSyncObjects()) return false;
    return true;
}

bool VulkanContext::LoadShaderModule(const char* path, VkShaderModule* outModule) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        lastError_ = std::string("Cannot open shader file: ") + path;
        return false;
    }
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> code(fileSize);
    file.seekg(0);
    file.read(code.data(), fileSize);
    file.close();

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule mod = nullptr;
    VkResult err = vkCreateShaderModule(device_, &createInfo, nullptr, &mod);
    if (err != VK_SUCCESS) {
        lastError_ = std::string("vkCreateShaderModule failed: ") + path + " result=" + std::to_string(err);
        return false;
    }
    *outModule = mod;
    return true;
}

bool VulkanContext::CreateImageViews() {
    swapchainImageViews_.resize(swapchainImages_.size());
    for (size_t i = 0; i < swapchainImages_.size(); ++i) {
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = static_cast<VkFormat>(swapchainFormat_);
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VkResult err = vkCreateImageView(device_, &createInfo, nullptr, &swapchainImageViews_[i]);
        if (err != VK_SUCCESS) {
            lastError_ = "vkCreateImageView failed index=" + std::to_string(i) + " result=" + std::to_string(err);
            return false;
        }
    }
    return true;
}

bool VulkanContext::CreateRenderPass() {
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = static_cast<VkFormat>(swapchainFormat_);
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef = {};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    VkResult err = vkCreateRenderPass(device_, &rpInfo, nullptr, &renderPass_);
    if (err != VK_SUCCESS) {
        lastError_ = "vkCreateRenderPass failed result=" + std::to_string(err);
        return false;
    }
    return true;
}

bool VulkanContext::CreateGraphicsPipeline(const char* shaderDir) {
    std::string vertPath = std::string(shaderDir) + "/triangle.vert.spv";
    std::string fragPath = std::string(shaderDir) + "/triangle.frag.spv";
    if (!LoadShaderModule(vertPath.c_str(), &vertShaderModule_)) return false;
    if (!LoadShaderModule(fragPath.c_str(), &fragShaderModule_)) return false;

    VkPipelineShaderStageCreateInfo vertStage = {};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertShaderModule_;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage = {};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragShaderModule_;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 0;
    vertexInput.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(swapchainWidth_);
    viewport.height = static_cast<float>(swapchainHeight_);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { 0, 0 };
    scissor.extent = { swapchainWidth_, swapchainHeight_ };

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pushConstantRangeCount = 0;

    VkResult err = vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_);
    if (err != VK_SUCCESS) {
        lastError_ = "vkCreatePipelineLayout failed result=" + std::to_string(err);
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    err = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline_);
    if (err != VK_SUCCESS) {
        lastError_ = "vkCreateGraphicsPipelines failed result=" + std::to_string(err);
        return false;
    }
    return true;
}

bool VulkanContext::CreateVertexBuffer() {
    // 三角形顶点在 vertex shader 中写死（positions[gl_VertexIndex]），无需 CPU 顶点数据
    // 但为了演示 Vertex Buffer 绑定与 Draw，创建一个最小 dummy buffer（3 个 float，或 0 size 有些驱动不接受）
    const float dummyVertices[] = { 0.0f, 0.0f, 0.0f };
    const VkDeviceSize bufferSize = sizeof(dummyVertices);

    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult err = vkCreateBuffer(device_, &bufferInfo, nullptr, &vertexBuffer_);
    if (err != VK_SUCCESS) {
        lastError_ = "vkCreateBuffer (vertex) failed result=" + std::to_string(err);
        return false;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device_, vertexBuffer_, &memReqs);

    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);

    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memReqs.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memTypeIndex = i;
            break;
        }
    }
    if (memTypeIndex == UINT32_MAX) {
        lastError_ = "No suitable memory type for vertex buffer";
        return false;
    }

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    err = vkAllocateMemory(device_, &allocInfo, nullptr, &vertexBufferMemory_);
    if (err != VK_SUCCESS) {
        lastError_ = "vkAllocateMemory (vertex) failed result=" + std::to_string(err);
        return false;
    }
    vkBindBufferMemory(device_, vertexBuffer_, vertexBufferMemory_, 0);

    void* data = nullptr;
    vkMapMemory(device_, vertexBufferMemory_, 0, bufferSize, 0, &data);
    memcpy(data, dummyVertices, bufferSize);
    vkUnmapMemory(device_, vertexBufferMemory_);
    return true;
}

bool VulkanContext::CreateFramebuffers() {
    framebuffers_.resize(swapchainImageViews_.size());
    for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
        VkImageView attachments[] = { swapchainImageViews_[i] };
        VkFramebufferCreateInfo fbInfo = {};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass_;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = swapchainWidth_;
        fbInfo.height = swapchainHeight_;
        fbInfo.layers = 1;

        VkResult err = vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffers_[i]);
        if (err != VK_SUCCESS) {
            lastError_ = "vkCreateFramebuffer failed index=" + std::to_string(i) + " result=" + std::to_string(err);
            return false;
        }
    }
    return true;
}

bool VulkanContext::CreateCommandPoolAndBuffers() {
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex_;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult err = vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_);
    if (err != VK_SUCCESS) {
        lastError_ = "vkCreateCommandPool failed result=" + std::to_string(err);
        return false;
    }

    commandBuffers_.resize(swapchainImages_.size());
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    err = vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data());
    if (err != VK_SUCCESS) {
        lastError_ = "vkAllocateCommandBuffers failed result=" + std::to_string(err);
        return false;
    }
    return true;
}

bool VulkanContext::CreateSyncObjects() {
    imageAvailableSemaphores_.resize(kMaxFramesInFlight);
    renderFinishedSemaphores_.resize(kMaxFramesInFlight);
    inFlightFences_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        VkResult err = vkCreateSemaphore(device_, &semInfo, nullptr, &imageAvailableSemaphores_[i]);
        if (err != VK_SUCCESS) {
            lastError_ = "vkCreateSemaphore imageAvailable failed result=" + std::to_string(err);
            return false;
        }
        err = vkCreateSemaphore(device_, &semInfo, nullptr, &renderFinishedSemaphores_[i]);
        if (err != VK_SUCCESS) {
            lastError_ = "vkCreateSemaphore renderFinished failed result=" + std::to_string(err);
            return false;
        }
        err = vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]);
        if (err != VK_SUCCESS) {
            lastError_ = "vkCreateFence failed result=" + std::to_string(err);
            return false;
        }
    }
    return true;
}

bool VulkanContext::AcquireNextImage(uint32_t& outImageIndex) {
    vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
    vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);

    uint32_t imageIndex = 0;
    VkResult err = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                          imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
        lastError_ = "AcquireNextImage OUT_OF_DATE_KHR";
        return false;
    }
    if (err != VK_SUCCESS && err != VK_SUBOPTIMAL_KHR) {
        lastError_ = "vkAcquireNextImageKHR failed result=" + std::to_string(err);
        return false;
    }
    outImageIndex = imageIndex;
    return true;
}

void VulkanContext::RecordCommandBuffer(uint32_t imageIndex) {
    VkCommandBuffer cmd = commandBuffers_[imageIndex];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkClearValue clearColor = {};
    clearColor.color = { { 0.0f, 0.0f, 0.1f, 1.0f } };

    VkRenderPassBeginInfo rpBegin = {};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = renderPass_;
    rpBegin.framebuffer = framebuffers_[imageIndex];
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = { swapchainWidth_, swapchainHeight_ };
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, offsets);
    vkCmdDraw(cmd, 3, 1, 0, 0);  // 3 vertices, 1 instance
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

bool VulkanContext::SubmitAndPresent(uint32_t imageIndex) {
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores_[currentFrame_] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores_[currentFrame_] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult err = vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]);
    if (err != VK_SUCCESS) {
        lastError_ = "vkQueueSubmit failed result=" + std::to_string(err);
        return false;
    }

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores_[currentFrame_];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;

    err = vkQueuePresentKHR(presentQueue_, &presentInfo);
    currentFrame_ = (currentFrame_ + 1) % kMaxFramesInFlight;

    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        return false;  // 调用方可选重建 Swapchain
    }
    if (err != VK_SUCCESS) {
        lastError_ = "vkQueuePresentKHR failed result=" + std::to_string(err);
        return false;
    }
    return true;
}

}  // namespace kale_device
