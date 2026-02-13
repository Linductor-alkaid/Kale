/**
 * @file test_multithread_record_passes.cpp
 * @brief phase9-9.5 多线程命令录制单元测试
 *
 * 验证：scheduler 为 nullptr 时单线程录制；SetScheduler 后按 GetTopologicalGroups 同组内并行录制；
 * RecordPasses 返回的 CommandList 数量与 Pass 数一致；Execute 多帧无死锁。
 */

#include <kale_pipeline/setup_render_graph.hpp>
#include <kale_pipeline/render_graph.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
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

class MultithreadRecordMockDevice : public kale_device::IRenderDevice {
public:
    std::uint32_t beginCommandListCalls = 0;

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

    kale_device::CommandList* BeginCommandList(std::uint32_t threadIndex) override {
        (void)threadIndex;
        beginCommandListCalls++;
        return &mockCmd_;
    }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}

    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override {
        kale_device::FenceHandle h;
        h.id = nextFenceId_++;
        return h;
    }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
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

/** scheduler 为 nullptr 时单线程录制，RecordPasses 返回的 cmd 数量等于拓扑序 Pass 数 */
static void test_record_passes_single_thread() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 256, 256);
    TEST_CHECK(rg.GetScheduler() == nullptr);

    MultithreadRecordMockDevice dev;
    TEST_CHECK(dev.Initialize({}));
    TEST_CHECK(rg.Compile(&dev));

    std::vector<kale_device::CommandList*> cmdLists = rg.RecordPasses(&dev);
    const size_t expectedPasses = rg.GetTopologicalOrder().size();
    TEST_CHECK(cmdLists.size() == expectedPasses);
    TEST_CHECK(dev.beginCommandListCalls == static_cast<std::uint32_t>(expectedPasses));
}

/** SetScheduler 后多线程录制，RecordPasses 返回的 cmd 数量与单线程一致 */
static void test_record_passes_with_scheduler() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler scheduler(&ex);

    kale::pipeline::RenderGraph rg;
    rg.SetScheduler(&scheduler);
    kale::pipeline::SetupRenderGraph(rg, 256, 256);
    TEST_CHECK(rg.GetScheduler() == &scheduler);

    MultithreadRecordMockDevice dev;
    TEST_CHECK(dev.Initialize({}));
    TEST_CHECK(rg.Compile(&dev));

    std::vector<kale_device::CommandList*> cmdLists = rg.RecordPasses(&dev);
    const size_t expectedPasses = rg.GetTopologicalOrder().size();
    TEST_CHECK(cmdLists.size() == expectedPasses);
    TEST_CHECK(dev.beginCommandListCalls == static_cast<std::uint32_t>(expectedPasses));
}

/** Execute 多帧（带 scheduler）无死锁 */
static void test_execute_multiframe_with_scheduler_no_deadlock() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler scheduler(&ex);

    kale::pipeline::RenderGraph rg;
    rg.SetScheduler(&scheduler);
    kale::pipeline::SetupRenderGraph(rg, 256, 256);

    MultithreadRecordMockDevice dev;
    TEST_CHECK(dev.Initialize({}));
    TEST_CHECK(rg.Compile(&dev));

    for (int i = 0; i < 5; ++i)
        rg.Execute(&dev);
}

/** SetScheduler(nullptr) 后退化为单线程 */
static void test_set_scheduler_null_fallback() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler scheduler(&ex);

    kale::pipeline::RenderGraph rg;
    rg.SetScheduler(&scheduler);
    kale::pipeline::SetupRenderGraph(rg, 256, 256);

    MultithreadRecordMockDevice dev;
    TEST_CHECK(dev.Initialize({}));
    TEST_CHECK(rg.Compile(&dev));

    rg.SetScheduler(nullptr);
    TEST_CHECK(rg.GetScheduler() == nullptr);
    std::vector<kale_device::CommandList*> cmdLists = rg.RecordPasses(&dev);
    TEST_CHECK(cmdLists.size() == rg.GetTopologicalOrder().size());
}

}  // namespace

int main() {
    test_record_passes_single_thread();
    test_record_passes_with_scheduler();
    test_execute_multiframe_with_scheduler_no_deadlock();
    test_set_scheduler_null_fallback();
    std::cout << "test_multithread_record_passes: all passed" << std::endl;
    return 0;
}
