// Hello Kale - 最小示例
// 验证 WindowSystem + Vulkan 基础：窗口、Vulkan Instance/Device/Surface/Swapchain

#include <kale_device/window_system.hpp>
#include <kale_device/vulkan_context.hpp>
#include <iostream>

int main() {
    kale_device::WindowSystem window;
    kale_device::WindowConfig config;
    config.width = 800;
    config.height = 600;
    config.title = "Hello Kale - Window + Vulkan";

    if (!window.Create(config)) {
        std::cerr << "WindowSystem::Create failed\n";
        return 1;
    }
    std::cout << "Window created: " << window.GetWidth() << "x" << window.GetHeight()
              << " title=\"" << config.title << "\"\n";

    kale_device::VulkanContext vulkan;
    kale_device::VulkanConfig vkConfig;
    vkConfig.windowHandle = window.GetNativeHandle();
    vkConfig.width = window.GetWidth();
    vkConfig.height = window.GetHeight();
    vkConfig.enableValidation = false;  // 设为 true 时需安装 Vulkan 验证层（如 Vulkan SDK）
    vkConfig.vsync = true;
    vkConfig.backBufferCount = 3;

    if (!vulkan.Initialize(vkConfig)) {
        std::cerr << "VulkanContext::Initialize failed: " << vulkan.GetLastError() << "\n";
        window.Destroy();
        return 1;
    }
    std::cout << "Vulkan: instance=" << vulkan.GetInstance()
              << " device=" << vulkan.GetDevice()
              << " swapchain images=" << vulkan.GetSwapchainImageCount()
              << " size=" << vulkan.GetSwapchainWidth() << "x" << vulkan.GetSwapchainHeight() << "\n";

    int frames = 0;
    while (window.PollEvents() && !window.ShouldClose()) {
        ++frames;
        if (frames <= 3 || frames % 60 == 0) {
            std::cout << "Frame " << frames << " size=" << window.GetWidth() << "x" << window.GetHeight() << "\n";
        }
    }

    vulkan.Shutdown();
    window.Destroy();
    std::cout << "Exited after " << frames << " frames.\n";
    return 0;
}
