// Hello Kale - 最小示例
// 验证 WindowSystem：创建窗口、事件循环、尺寸与标题

#include <kale_device/window_system.hpp>
#include <iostream>

int main() {
    kale_device::WindowSystem window;
    kale_device::WindowConfig config;
    config.width = 800;
    config.height = 600;
    config.title = "Hello Kale - Window Test";

    if (!window.Create(config)) {
        std::cerr << "WindowSystem::Create failed\n";
        return 1;
    }
    std::cout << "Window created: " << window.GetWidth() << "x" << window.GetHeight()
              << " title=\"" << config.title << "\"\n";

    int frames = 0;
    while (window.PollEvents() && !window.ShouldClose()) {
        ++frames;
        if (frames <= 3 || frames % 60 == 0) {
            std::cout << "Frame " << frames << " size=" << window.GetWidth() << "x" << window.GetHeight() << "\n";
        }
    }

    window.Destroy();
    std::cout << "Exited after " << frames << " frames.\n";
    return 0;
}
