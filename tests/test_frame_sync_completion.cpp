/**
 * @file test_frame_sync_completion.cpp
 * @brief phase13-13.19 帧流水线同步完善单元测试
 *
 * 验证：Execute 调用 AcquireNextImage 后 Submit(cmdLists, {}, {}, FenceHandle{})；
 * 空 wait/signal/fence 时由设备使用帧 imageAvailable/renderFinished/Fence；
 * RDI Submit 接口支持 (cmdLists, waitSemaphores, signalSemaphores, fence)。
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

/** Mock：记录 Submit 的 wait/signal/fence 是否为空或无效，以及 Acquire 是否在 Submit 前调用 */
class FrameSyncMockDevice : public kale_device::IRenderDevice {
public:
    bool submitCalledWithEmptyWait = false;
    bool submitCalledWithEmptySignal = false;
    bool submitCalledWithInvalidFence = false;
    bool acquireBeforeSubmit = false;
    int acquireCallOrder = 0;
    int submitCallOrder = 0;

    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
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

    void UpdateBuffer(kale_device::BufferHandle, const void*, std::size_t, std::size_t) override {}
    void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override { return nullptr; }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}

    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &mockCmd_; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>& waitSemaphores,
                const std::vector<kale_device::SemaphoreHandle>& signalSemaphores,
                kale_device::FenceHandle fence) override {
        submitCallOrder = ++callOrder_;
        submitCalledWithEmptyWait = waitSemaphores.empty();
        submitCalledWithEmptySignal = signalSemaphores.empty();
        submitCalledWithInvalidFence = !fence.IsValid();
        acquireBeforeSubmit = (acquireCallOrder > 0 && acquireCallOrder < submitCallOrder);
    }

    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override { return {}; }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }

    std::uint32_t AcquireNextImage() override {
        acquireCallOrder = ++callOrder_;
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
    int callOrder_ = 0;
    MockCommandList mockCmd_;
};

/** Execute 调用 Submit 时传入空 wait、空 signal、无效 fence，由设备使用帧信号 */
static void test_submit_with_empty_sync() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 256, 256);
    FrameSyncMockDevice dev;
    TEST_CHECK(dev.Initialize({}));
    TEST_CHECK(rg.Compile(&dev));
    rg.Execute(&dev);

    TEST_CHECK(dev.submitCalledWithEmptyWait);
    TEST_CHECK(dev.submitCalledWithEmptySignal);
    TEST_CHECK(dev.submitCalledWithInvalidFence);
}

/** AcquireNextImage 在 Submit 之前被调用 */
static void test_acquire_before_submit() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 256, 256);
    FrameSyncMockDevice dev;
    TEST_CHECK(dev.Initialize({}));
    TEST_CHECK(rg.Compile(&dev));
    rg.Execute(&dev);

    TEST_CHECK(dev.acquireBeforeSubmit);
}

}  // namespace

int main() {
    test_submit_with_empty_sync();
    test_acquire_before_submit();
    std::cout << "test_frame_sync_completion: all passed" << std::endl;
    return 0;
}
