/**
 * @file test_frame_data_main_loop_integration.cpp
 * @brief FrameData 与引擎主循环集成测试（phase13-13.1）
 *
 * 验证：主循环帧末对 GetVisibleObjectsFrameData() 调用 end_frame()，
 * 使本帧写入 write_buffer() 的数据在 Run() 返回后对 read_buffer() 可见。
 */

#include <kale_engine/render_engine.hpp>
#include <kale_executor/render_task_scheduler.hpp>

#include <cstdlib>
#include <iostream>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

namespace {

// 在 OnRender 中向 FrameData write_buffer 写入，然后 RequestQuit；Run 帧末会 end_frame
struct FrameDataWriterApp : kale::IApplication {
    kale::RenderEngine* engine = nullptr;
    void* sentinelNode = reinterpret_cast<void*>(0x1234u);

    void OnUpdate(float) override {}

    void OnRender() override {
        kale::executor::RenderTaskScheduler* sched = engine ? engine->GetScheduler() : nullptr;
        if (!sched) return;
        kale::executor::FrameData<kale::executor::VisibleObjectList>* fd = sched->GetVisibleObjectsFrameData();
        if (!fd) return;
        fd->write_buffer().nodes.clear();
        fd->write_buffer().nodes.push_back(sentinelNode);
        engine->RequestQuit();
    }
};

}  // namespace

int main() {
    kale::RenderEngine engine;
    kale::RenderEngine::Config config;
    config.width = 320;
    config.height = 240;
    config.title = "FrameDataIntegration";
    config.enableValidation = false;

    if (!engine.Initialize(config)) {
        std::cerr << "Skip: Initialize failed (no display?)\n";
        return 0;
    }

    kale::executor::FrameData<kale::executor::VisibleObjectList>* fd = engine.GetScheduler()->GetVisibleObjectsFrameData();
    TEST_CHECK(fd != nullptr);

    FrameDataWriterApp app;
    app.engine = &engine;
    engine.Run(&app);

    // Run() 在每帧 Present 后调用了 end_frame()，故本帧写入的 write_buffer 已变为 read_buffer
    const kale::executor::VisibleObjectList& read = fd->read_buffer();
    TEST_CHECK(read.nodes.size() == 1u);
    TEST_CHECK(read.nodes[0] == app.sentinelNode);

    std::cout << "test_frame_data_main_loop_integration passed.\n";
    return 0;
}
