// Hello Kale - 最小示例
// 验证 WindowSystem + Vulkan 基础 + 简单三角形渲染（phase1-1.3）

#include <kale_device/window_system.hpp>
#include <kale_device/vulkan_context.hpp>
#include <iostream>

int main() {
    kale_device::WindowSystem window;
    kale_device::WindowConfig config;
    config.width = 800;
    config.height = 600;
    config.title = "Hello Kale - Triangle";

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
    vkConfig.enableValidation = false;
    vkConfig.vsync = true;
    vkConfig.backBufferCount = 3;

    if (!vulkan.Initialize(vkConfig)) {
        std::cerr << "VulkanContext::Initialize failed: " << vulkan.GetLastError() << "\n";
        window.Destroy();
        return 1;
    }
    std::cout << "Vulkan: swapchain images=" << vulkan.GetSwapchainImageCount()
              << " size=" << vulkan.GetSwapchainWidth() << "x" << vulkan.GetSwapchainHeight() << "\n";

    // 尝试多个着色器路径：从 build/apps/hello_kale 运行用 "shaders"，从 build 运行用 "apps/hello_kale/shaders"
    if (!vulkan.CreateTriangleRendering("shaders") &&
        !vulkan.CreateTriangleRendering("apps/hello_kale/shaders")) {
        std::cerr << "CreateTriangleRendering failed: " << vulkan.GetLastError() << "\n";
        vulkan.Shutdown();
        window.Destroy();
        return 1;
    }
    std::cout << "Triangle rendering initialized.\n";

    int frames = 0;
    while (window.PollEvents() && !window.ShouldClose()) {
        uint32_t imageIndex = 0;
        if (!vulkan.AcquireNextImage(imageIndex)) {
            continue;  // OUT_OF_DATE 等，跳过本帧
        }
        vulkan.RecordCommandBuffer(imageIndex);
        if (!vulkan.SubmitAndPresent(imageIndex)) {
            continue;
        }
        ++frames;
        if (frames <= 3 || frames % 60 == 0) {
            std::cout << "Frame " << frames << "\n";
        }
    }

    vulkan.Shutdown();
    window.Destroy();
    std::cout << "Exited after " << frames << " frames.\n";
    return 0;
}
