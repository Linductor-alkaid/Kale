/**
 * @file test_multi_viewport_output_target.cpp
 * @brief phase12-12.6 多相机/多视口：SetOutputTarget / Execute(device, target) 单元测试
 *
 * 覆盖：SetOutputTarget/GetOutputTarget 默认无效与设置；Execute(device, outputTarget) 本帧使用覆盖目标；
 * RenderPassContext::GetOutputTarget 在 Pass 执行时返回覆盖目标；多视口方案 C（多 RG + 可选离屏目标）可验证。
 */

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/render_pass_builder.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <iostream>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__       \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                               \
    } while (0)

namespace {

static kale_device::TextureHandle g_capturedOutputTarget;

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

class MockDevice : public kale_device::IRenderDevice {
public:
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
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}

    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override {
        kale_device::FenceHandle h;
        h.id = ++nextFenceId_;
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
        h.id = kBackBufferId;
        return h;
    }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }
    void SetExtent(std::uint32_t, std::uint32_t) override {}

    static constexpr std::uint64_t kBackBufferId = 1;

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextTexId_ = 0;
    std::uint64_t nextFenceId_ = 0;
    MockCommandList mockCmd_;
};

static void test_set_get_output_target() {
    kale::pipeline::RenderGraph rg;
    TEST_CHECK(!rg.GetOutputTarget().IsValid());

    kale_device::TextureHandle offscreen;
    offscreen.id = 42;
    rg.SetOutputTarget(offscreen);
    TEST_CHECK(rg.GetOutputTarget().IsValid());
    TEST_CHECK(rg.GetOutputTarget().id == 42u);

    rg.SetOutputTarget(kale_device::TextureHandle{});
    TEST_CHECK(!rg.GetOutputTarget().IsValid());
}

static void test_execute_with_output_target_override() {
    g_capturedOutputTarget = kale_device::TextureHandle{};
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    rg.AddPass(
        "Out",
        [](kale::pipeline::RenderPassBuilder& b) { b.WriteSwapchain(); },
        [](const kale::pipeline::RenderPassContext& ctx, kale_device::CommandList&) {
            g_capturedOutputTarget = ctx.GetOutputTarget();
        });

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));

    kale_device::TextureHandle offscreen;
    offscreen.id = 100;
    rg.Execute(&dev, offscreen);
    TEST_CHECK(g_capturedOutputTarget.IsValid());
    TEST_CHECK(g_capturedOutputTarget.id == 100u);

    rg.Execute(&dev);
    TEST_CHECK(!g_capturedOutputTarget.IsValid());
}

static void test_execute_device_target_then_restore() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(32, 32);
    rg.AddPass(
        "S",
        [](kale::pipeline::RenderPassBuilder& b) { b.WriteSwapchain(); },
        [](const kale::pipeline::RenderPassContext& ctx, kale_device::CommandList&) {
            g_capturedOutputTarget = ctx.GetOutputTarget();
        });

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));

    kale_device::TextureHandle custom;
    custom.id = 200;
    rg.Execute(&dev, custom);
    TEST_CHECK(g_capturedOutputTarget.id == 200u);

    rg.Execute(&dev);
    TEST_CHECK(!g_capturedOutputTarget.IsValid());
}

static void test_context_get_output_target_without_graph() {
    kale::pipeline::RenderPassContext ctx(nullptr, nullptr, nullptr);
    kale_device::TextureHandle h = ctx.GetOutputTarget();
    TEST_CHECK(!h.IsValid());
}

}  // namespace

int main() {
    test_set_get_output_target();
    test_execute_with_output_target_override();
    test_execute_device_target_then_restore();
    test_context_get_output_target_without_graph();
    std::cout << "test_multi_viewport_output_target OK\n";
    return 0;
}
