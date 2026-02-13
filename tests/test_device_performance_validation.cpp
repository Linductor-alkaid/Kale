/**
 * @file test_device_performance_validation.cpp
 * @brief phase13-13.9 设备抽象层性能测试与调优验证
 *
 * 验证：帧循环中无每帧 WaitIdle，仅使用 Fence 同步；多 DrawCall、多线程录制不触发 WaitIdle 且无崩溃。
 */

#include <kale_pipeline/setup_render_graph.hpp>
#include <kale_pipeline/render_graph.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_scene/scene_types.hpp>
#include <kale_scene/renderable.hpp>
#include <kale_executor/render_task_scheduler.hpp>
#include <executor/executor.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__       \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                               \
    } while (0)

namespace {

class MockCommandList : public kale_device::CommandList {
public:
    void BeginRenderPass(const std::vector<kale_device::TextureHandle>&,
                         kale_device::TextureHandle) override {}
    void EndRenderPass() override {}
    void BindPipeline(kale_device::PipelineHandle) override {}
    void BindDescriptorSet(std::uint32_t, kale_device::DescriptorSetHandle) override {}
    void BindVertexBuffer(std::uint32_t, kale_device::BufferHandle, std::size_t) override {}
    void BindIndexBuffer(kale_device::BufferHandle, std::size_t, bool) override {}
    void SetPushConstants(const void*, std::size_t, std::size_t) override {}
    void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
    void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void CopyBufferToBuffer(kale_device::BufferHandle, std::size_t,
                            kale_device::BufferHandle, std::size_t, std::size_t) override {}
    void CopyBufferToTexture(kale_device::BufferHandle, std::size_t,
                             kale_device::TextureHandle, std::uint32_t,
                             std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void CopyTextureToTexture(kale_device::TextureHandle, kale_device::TextureHandle,
                              std::uint32_t, std::uint32_t) override {}
    void Barrier(const std::vector<kale_device::TextureHandle>&) override {}
    void ClearColor(kale_device::TextureHandle, const float*) override {}
    void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
};

/** Mock 设备：统计 WaitIdle 调用次数，验证帧路径中不调用 WaitIdle（仅用 Fence 同步） */
class PerformanceValidationMockDevice : public kale_device::IRenderDevice {
public:
    std::uint32_t waitIdleCallCount = 0;
    std::uint32_t waitForFenceCallCount = 0;
    std::uint32_t resetFenceCallCount = 0;

    bool Initialize(const kale_device::DeviceConfig&) override {
        caps_.maxRecordingThreads = 4;
        return true;
    }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override { return {}; }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override {
        kale_device::TextureHandle h;
        h.id = ++nextTexId_;
        return h;
    }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }
    kale_device::DescriptorSetHandle CreateDescriptorSet(const kale_device::DescriptorSetLayoutDesc&) override { return {}; }
    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::BufferHandle, std::size_t, std::size_t) override {}

    void DestroyBuffer(kale_device::BufferHandle) override {}
    void DestroyTexture(kale_device::TextureHandle) override {}
    void DestroyShader(kale_device::ShaderHandle) override {}
    void DestroyPipeline(kale_device::PipelineHandle) override {}
    void DestroyDescriptorSet(kale_device::DescriptorSetHandle) override {}

    void UpdateBuffer(kale_device::BufferHandle, const void*, std::size_t, std::size_t = 0) override {}
    void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override { return nullptr; }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}

    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &mockCmd_; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}

    void WaitIdle() override { waitIdleCallCount++; }
    kale_device::FenceHandle CreateFence(bool) override {
        kale_device::FenceHandle h;
        h.id = ++nextFenceId_;
        return h;
    }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override { waitForFenceCallCount++; }
    void ResetFence(kale_device::FenceHandle) override { resetFenceCallCount++; }
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }

    std::uint32_t AcquireNextImage() override { return 0; }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override {
        kale_device::TextureHandle h;
        h.id = 1;
        return h;
    }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }
    void SetExtent(std::uint32_t, std::uint32_t) override {}

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextTexId_ = 0;
    std::uint64_t nextFenceId_ = 0;
    MockCommandList mockCmd_;
};

/** 帧循环中不得调用 WaitIdle，仅使用 Fence 同步 */
static void test_no_wait_idle_in_frame_loop() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 256, 256);

    PerformanceValidationMockDevice dev;
    TEST_CHECK(dev.Initialize({}));
    TEST_CHECK(rg.Compile(&dev));

    const std::uint32_t kFrames = 15;
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        rg.Execute(&dev);
    }

    TEST_CHECK(dev.waitIdleCallCount == 0);
}

/** 多 DrawCall 提交后多帧 Execute，仍不触发 WaitIdle */
static void test_many_draws_no_wait_idle() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 256, 256);

    PerformanceValidationMockDevice dev;
    TEST_CHECK(dev.Initialize({}));
    TEST_CHECK(rg.Compile(&dev));

    kale::resource::BoundingBox box;
    box.min = glm::vec3(-1.f);
    box.max = glm::vec3(1.f);
    class DummyRenderable : public kale::scene::Renderable {
    public:
        explicit DummyRenderable(const kale::resource::BoundingBox& b) { bounds_ = b; }
        kale::resource::BoundingBox GetBounds() const override { return bounds_; }
        void Draw(kale_device::CommandList&, const glm::mat4&, kale_device::IRenderDevice*) override {}
    };
    std::vector<std::unique_ptr<DummyRenderable>> renderables;
    const std::uint32_t kManyDraws = 100;
    for (std::uint32_t i = 0; i < kManyDraws; ++i) {
        renderables.push_back(std::make_unique<DummyRenderable>(box));
    }

    for (std::uint32_t frame = 0; frame < 5; ++frame) {
        rg.ClearSubmitted();
        glm::mat4 id(1.f);
        for (auto& r : renderables)
            rg.SubmitRenderable(r.get(), id, kale::scene::PassFlags::Opaque);
        rg.Execute(&dev);
    }

    TEST_CHECK(dev.waitIdleCallCount == 0);
}

/** 多线程录制多帧 Execute，不触发 WaitIdle */
static void test_multithread_record_no_wait_idle() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler scheduler(&ex);
    kale::pipeline::RenderGraph rg;
    rg.SetScheduler(&scheduler);
    kale::pipeline::SetupRenderGraph(rg, 256, 256);

    PerformanceValidationMockDevice dev;
    TEST_CHECK(dev.Initialize({}));
    TEST_CHECK(rg.Compile(&dev));

    const std::uint32_t kFrames = 8;
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        rg.Execute(&dev);
    }

    TEST_CHECK(dev.waitIdleCallCount == 0);
}

/** 验证帧路径使用 Fence 同步：AcquireNextImage 可由设备内部 WaitForFence/ResetFence，本 mock 不模拟；仅确保 Execute 路径不调用 WaitIdle */
static void test_fence_sync_not_wait_idle() {
    PerformanceValidationMockDevice dev;
    TEST_CHECK(dev.Initialize({}));

    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 128, 128);
    TEST_CHECK(rg.Compile(&dev));

    for (int i = 0; i < 10; ++i)
        rg.Execute(&dev);

    TEST_CHECK(dev.waitIdleCallCount == 0);
}

}  // namespace

int main() {
    test_no_wait_idle_in_frame_loop();
    test_many_draws_no_wait_idle();
    test_multithread_record_no_wait_idle();
    test_fence_sync_not_wait_idle();
    std::cout << "test_device_performance_validation OK\n";
    return 0;
}
