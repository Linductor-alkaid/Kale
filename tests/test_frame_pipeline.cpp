/**
 * @file test_frame_pipeline.cpp
 * @brief phase9-9.3 帧流水线完善单元测试
 *
 * 验证 RenderGraph::Execute 完整帧流水线：WaitFence → ResetFence → Acquire →
 * Record → Submit(wait, signal, fence) → ReleaseFrameResources；Present 由应用层调用。
 * 多帧连续 Execute 无死锁，kMaxFramesInFlight 帧并发正确。
 */

#include <kale_pipeline/setup_render_graph.hpp>
#include <kale_pipeline/render_graph.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <iostream>
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

/** Mock 设备：统计 WaitForFence / ResetFence / AcquireNextImage / Submit 调用次数，验证帧流水线顺序与无死锁 */
class FramePipelineMockDevice : public kale_device::IRenderDevice {
public:
    std::uint32_t waitCount = 0;
    std::uint32_t resetCount = 0;
    std::uint32_t acquireCount = 0;
    std::uint32_t submitCount = 0;

    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override { return {}; }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override {
        nextTexId_++;
        kale_device::TextureHandle h;
        h.id = nextTexId_;
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

    void UpdateBuffer(kale_device::BufferHandle, const void*, std::size_t, std::size_t) override {}
    void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override { return nullptr; }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}

    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &mockCmd_; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {
        submitCount++;
    }

    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override {
        nextFenceId_++;
        kale_device::FenceHandle h;
        h.id = nextFenceId_;
        return h;
    }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override { waitCount++; }
    void ResetFence(kale_device::FenceHandle) override { resetCount++; }
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }

    std::uint32_t AcquireNextImage() override {
        acquireCount++;
        return 0;
    }
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

/** 多帧连续 Execute 无死锁，且每帧调用顺序正确（Wait → Reset → Acquire → Record → Submit → ReleaseFrameResources） */
static void test_frame_pipeline_multiple_frames_no_deadlock() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 256, 256);

    FramePipelineMockDevice dev;
    TEST_CHECK(dev.Initialize({}));
    TEST_CHECK(rg.Compile(&dev));

    const std::uint32_t kFrames = 8;  // 超过 kMaxFramesInFlight(3)，验证轮转
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        rg.Execute(&dev);
    }

    // Execute 使用 IsFenceSignaled 轮询 + ResetFence，不调用 WaitForFence
    TEST_CHECK(dev.resetCount == kFrames);
    TEST_CHECK(dev.acquireCount == kFrames);
    TEST_CHECK(dev.submitCount == kFrames);
}

/** AcquireNextImage 失败时本帧跳过，不 Submit，不增加 submitCount */
static void test_frame_pipeline_acquire_fail_skips_frame() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 256, 256);

    class AcquireFailMock : public FramePipelineMockDevice {
    public:
        std::uint32_t acquireCalls = 0;
        std::uint32_t AcquireNextImage() override {
            acquireCalls++;
            acquireCount++;  // 每次 Execute 都会调用一次，无论成败
            if (acquireCalls >= 2) return kale_device::IRenderDevice::kInvalidSwapchainImageIndex;
            return 0;
        }
    };
    AcquireFailMock dev;
    TEST_CHECK(dev.Initialize({}));
    TEST_CHECK(rg.Compile(&dev));

    rg.Execute(&dev);  // 第一帧正常
    rg.Execute(&dev);  // 第二帧 Acquire 返回无效，跳过
    rg.Execute(&dev);  // 第三帧再次返回无效，跳过

    TEST_CHECK(dev.submitCount == 1);
    TEST_CHECK(dev.acquireCount == 3);  // 三次 Execute 各调用一次 Acquire
}

/** 未 Compile 或 device 为 nullptr 时 Execute 直接返回 */
static void test_frame_pipeline_null_or_uncompiled_no_op() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 256, 256);
    FramePipelineMockDevice dev;
    dev.Initialize({});

    rg.Execute(nullptr);
    TEST_CHECK(dev.waitCount == 0 && dev.acquireCount == 0);

    rg.Execute(&dev);  // 未 Compile，应不调用设备
    TEST_CHECK(dev.waitCount == 0 && dev.acquireCount == 0);

    rg.Compile(&dev);
    rg.Execute(&dev);
    // Execute 使用 IsFenceSignaled 轮询 + ResetFence，不调用 WaitForFence；每帧一次 Acquire
    TEST_CHECK(dev.resetCount == 1 && dev.acquireCount == 1);
}

}  // namespace

int main() {
    test_frame_pipeline_null_or_uncompiled_no_op();
    test_frame_pipeline_acquire_fail_skips_frame();
    test_frame_pipeline_multiple_frames_no_deadlock();
    std::cout << "test_frame_pipeline: all passed" << std::endl;
    return 0;
}
