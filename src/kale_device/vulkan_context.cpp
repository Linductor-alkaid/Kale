// Kale 设备抽象层 - Vulkan 基础上下文实现

#include <kale_device/vulkan_context.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>

#include <vulkan/vulkan.h>

#include <cstring>

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

VkImage VulkanContext::GetSwapchainImage(uint32_t index) const {
    if (index >= swapchainImages_.size()) return nullptr;
    return swapchainImages_[index];
}

}  // namespace kale_device
