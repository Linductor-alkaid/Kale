/**
 * @file test_window_resize_swapchain.cpp
 * @brief phase11-11.1 窗口 Resize 与 Swapchain 单元测试
 *
 * 验证：WindowSystem::Resize/GetWidth/GetHeight；Run 中尺寸变化时 SetExtent 路径与最小化跳过渲染不崩溃。
 */

#include <kale_device/window_system.hpp>
#include <kale_engine/render_engine.hpp>
#include <cassert>
#include <cstdint>

using namespace kale_device;
using namespace kale;

namespace {

void test_window_system_resize_and_size() {
    WindowSystem ws;
    WindowConfig wc;
    wc.width = 320;
    wc.height = 240;
    wc.title = "ResizeTest";
    wc.resizable = true;
    assert(ws.Create(wc));
    assert(ws.GetWidth() == 320);
    assert(ws.GetHeight() == 240);
    ws.Resize(400, 300);
    assert(ws.GetWidth() == 400);
    assert(ws.GetHeight() == 300);
    ws.Destroy();
    assert(ws.GetWidth() == 0);
    assert(ws.GetHeight() == 0);
}

void test_render_engine_run_after_resize_no_crash() {
    RenderEngine engine;
    RenderEngine::Config config;
    config.width = 320;
    config.height = 240;
    config.title = "ResizeRun";
    config.enableValidation = false;
    if (!engine.Initialize(config)) return;

    WindowSystem* win = engine.GetWindowSystem();
    assert(win && win->GetWidth() == 320 && win->GetHeight() == 240);
    win->Resize(400, 300);
    assert(win->GetWidth() == 400 && win->GetHeight() == 300);

    struct QuitAfterTwo : IApplication {
        RenderEngine* e = nullptr;
        int frames = 0;
        void OnUpdate(float) override {
            if (e && ++frames >= 2) e->RequestQuit();
        }
        void OnRender() override {}
    };
    QuitAfterTwo app;
    app.e = &engine;
    engine.Run(&app);
    assert(app.frames >= 2);
}

void test_run_minimize_skip_render_no_crash() {
    RenderEngine engine;
    RenderEngine::Config config;
    config.width = 320;
    config.height = 240;
    config.enableValidation = false;
    if (!engine.Initialize(config)) return;
    WindowSystem* win = engine.GetWindowSystem();
    assert(win);
    win->Resize(0, 0);
    bool zeroSize = (win->GetWidth() == 0 || win->GetHeight() == 0);
    struct QuitFirst : IApplication {
        RenderEngine* e = nullptr;
        int updates = 0, renders = 0;
        void OnUpdate(float) override {
            ++updates;
            if (e) e->RequestQuit();
        }
        void OnRender() override { ++renders; }
    };
    QuitFirst app;
    app.e = &engine;
    engine.Run(&app);
    assert(app.updates >= 1);
    if (zeroSize) assert(app.renders == 0);
}

}  // namespace

int main() {
    test_window_system_resize_and_size();
    test_render_engine_run_after_resize_no_crash();
    test_run_minimize_skip_render_no_crash();
    return 0;
}
