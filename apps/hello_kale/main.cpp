// Hello Kale - 最小示例
// 验证 WindowSystem + Vulkan(RDI) + RenderGraph 简单 Forward Pass + 输入（phase1-1.4, phase6-6.13）

#include <kale_device/window_system.hpp>
#include <kale_device/input_manager.hpp>
#include <kale_device/render_device.hpp>
#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/forward_pass.hpp>
#include <iostream>

int main() {
    kale_device::WindowSystem window;
    kale_device::WindowConfig config;
    config.width = 800;
    config.height = 600;
    config.title = "Hello Kale - Forward Pass";

    if (!window.Create(config)) {
        std::cerr << "WindowSystem::Create failed\n";
        return 1;
    }
    std::cout << "Window created: " << window.GetWidth() << "x" << window.GetHeight()
              << " title=\"" << config.title << "\"\n";

    auto device = kale_device::CreateRenderDevice(kale_device::Backend::Vulkan);
    if (!device) {
        std::cerr << "CreateRenderDevice failed\n";
        window.Destroy();
        return 1;
    }
    kale_device::DeviceConfig devConfig;
    devConfig.windowHandle = window.GetNativeHandle();
    devConfig.width = window.GetWidth();
    devConfig.height = window.GetHeight();
    devConfig.enableValidation = false;
    devConfig.vsync = true;
    devConfig.backBufferCount = 3;

    if (!device->Initialize(devConfig)) {
        std::cerr << "IRenderDevice::Initialize failed: " << device->GetLastError() << "\n";
        window.Destroy();
        return 1;
    }
    std::cout << "Vulkan RDI initialized.\n";

    kale::pipeline::RenderGraph rg;
    rg.SetResolution(static_cast<std::uint32_t>(window.GetWidth()),
                     static_cast<std::uint32_t>(window.GetHeight()));
    kale::pipeline::SetupForwardOnlyRenderGraph(rg);
    if (!rg.Compile(device.get())) {
        std::cerr << "RenderGraph::Compile failed: " << rg.GetLastError() << "\n";
        device->Shutdown();
        window.Destroy();
        return 1;
    }
    std::cout << "RenderGraph compiled (Forward Pass only).\n";

    kale_device::InputManager input;
    input.Initialize(window.GetWindow());

    int frames = 0;
    while (!input.QuitRequested()) {
        input.Update();
        if (input.IsKeyJustPressed(kale_device::KeyCode::Escape)) {
            break;
        }
        rg.ClearSubmitted();
        rg.Execute(device.get());
        device->Present();
        ++frames;
        if (frames <= 3 || frames % 60 == 0) {
            std::cout << "Frame " << frames << "\n";
        }
    }

    device->Shutdown();
    window.Destroy();
    std::cout << "Exited after " << frames << " frames.\n";
    return 0;
}
