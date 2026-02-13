/**
 * @file test_render_engine_run.cpp
 * @brief RenderEngine::Run() 主循环单元测试（phase11-11.9）
 *
 * 验证：Run() 顺序、RequestQuit、null 安全、空 app 不崩溃。
 */

#include <kale_engine/render_engine.hpp>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>

using namespace kale;

namespace {

// 记录调用顺序与 deltaTime
struct RecordingApp : IApplication {
    std::vector<std::string> order;
    float lastDeltaTime = -1.f;
    int onUpdateCount = 0;
    int onRenderCount = 0;
    bool requestQuitAfterFrames = false;
    int framesUntilQuit = 1;
    RenderEngine* engine = nullptr;

    void OnUpdate(float deltaTime) override {
        order.push_back("OnUpdate");
        lastDeltaTime = deltaTime;
        ++onUpdateCount;
        if (requestQuitAfterFrames && engine && onUpdateCount >= framesUntilQuit)
            engine->RequestQuit();
    }

    void OnRender() override {
        order.push_back("OnRender");
        ++onRenderCount;
    }
};

void test_run_order_and_request_quit() {
    RenderEngine engine;
    RenderEngine::Config config;
    config.width = 320;
    config.height = 240;
    config.title = "TestRun";
    config.enableValidation = false;
    if (!engine.Initialize(config)) {
        assert(false && "Initialize failed");
        return;
    }
    RecordingApp app;
    app.engine = &engine;
    app.requestQuitAfterFrames = true;
    app.framesUntilQuit = 2;
    engine.Run(&app);
    // 每帧顺序应为 OnUpdate -> OnRender，且至少 2 帧
    assert(app.onUpdateCount >= 2 && app.onRenderCount >= 2);
    assert(app.onUpdateCount == app.onRenderCount);
    assert(app.lastDeltaTime >= 0.f);
    size_t n = app.order.size();
    assert(n >= 4u);
    for (size_t i = 0; i + 1 < n; i += 2) {
        assert(app.order[i] == "OnUpdate");
        assert(app.order[i + 1] == "OnRender");
    }
}

void test_run_null_app_no_crash() {
    RenderEngine engine;
    RenderEngine::Config config;
    config.width = 320;
    config.height = 240;
    config.enableValidation = false;
    if (!engine.Initialize(config)) return;
    engine.Run(nullptr);
    // 不应崩溃，立即返回
}

void test_run_uninitialized_no_crash() {
    RenderEngine engine;
    RecordingApp app;
    engine.Run(&app);
    // 未 Initialize，Run 应直接返回
    assert(app.onUpdateCount == 0 && app.onRenderCount == 0);
}

void test_request_quit_before_run_no_crash() {
    RenderEngine engine;
    engine.RequestQuit();
    RenderEngine::Config config;
    config.width = 320;
    config.height = 240;
    config.enableValidation = false;
    if (!engine.Initialize(config)) return;
    RecordingApp app;
    engine.Run(&app);
    // RequestQuit 在 Run 开始时被重置，所以会正常跑直到 SDL 或再次 RequestQuit
    // 此处仅验证 Run 不崩溃
}

void test_delta_time_non_negative() {
    RenderEngine engine;
    RenderEngine::Config config;
    config.width = 320;
    config.height = 240;
    config.enableValidation = false;
    if (!engine.Initialize(config)) return;
    RecordingApp app;
    app.engine = &engine;
    app.requestQuitAfterFrames = true;
    app.framesUntilQuit = 1;
    engine.Run(&app);
    assert(app.onUpdateCount >= 1);
    assert(app.lastDeltaTime >= 0.f);
}

}  // namespace

int main() {
    test_run_null_app_no_crash();
    test_run_uninitialized_no_crash();
    test_request_quit_before_run_no_crash();
    test_delta_time_non_negative();
    test_run_order_and_request_quit();
    return 0;
}
