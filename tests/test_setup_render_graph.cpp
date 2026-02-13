/**
 * @file test_setup_render_graph.cpp
 * @brief phase8-8.9 SetupRenderGraph 示例单元测试
 *
 * 覆盖：SetupRenderGraph 设置分辨率并添加完整 Deferred+Shadow 管线；
 * Pass 依赖 DAG 为 Shadow → GBuffer → Lighting → PostProcess → OutputToSwapchain；
 * Compile 成功、拓扑序 5、分辨率与 DeclareTexture 一致；
 * Execute 不崩溃。
 */

#include <kale_pipeline/setup_render_graph.hpp>
#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
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

class MockDevice : public kale_device::IRenderDevice {
public:
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
                kale_device::FenceHandle) override {}

    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override {
        nextFenceId_++;
        kale_device::FenceHandle h;
        h.id = nextFenceId_;
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

static const std::vector<std::string> kExpectedDeferredPassOrder = {
    "ShadowPass",
    "GBufferPass",
    "LightingPass",
    "PostProcess",
    "OutputToSwapchain"
};

static void test_setup_render_graph_resolution_and_dag() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 1920, 1080);

    TEST_CHECK(rg.GetResolutionWidth() == 1920u);
    TEST_CHECK(rg.GetResolutionHeight() == 1080u);
    TEST_CHECK(rg.GetHandleByName("ShadowMap") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("FinalColor") != kale::pipeline::kInvalidRGResourceHandle);

    MockDevice dev;
    dev.Initialize({});
    bool ok = rg.Compile(&dev);
    TEST_CHECK(ok);
    TEST_CHECK(rg.IsCompiled());
    TEST_CHECK(rg.GetLastError().empty());

    const auto& order = rg.GetTopologicalOrder();
    TEST_CHECK(order.size() == kExpectedDeferredPassOrder.size());
    const auto& passes = rg.GetPasses();
    for (size_t i = 0; i < order.size(); ++i) {
        std::size_t idx = static_cast<std::size_t>(order[i]);
        TEST_CHECK(idx < passes.size());
        TEST_CHECK(passes[idx].name == kExpectedDeferredPassOrder[i]);
    }
}

static void test_setup_render_graph_custom_shadow_size() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 800, 600, 1024u);

    TEST_CHECK(rg.GetResolutionWidth() == 800u);
    TEST_CHECK(rg.GetResolutionHeight() == 600u);

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));
    TEST_CHECK(rg.GetTopologicalOrder().size() == 5u);
}

static void test_setup_render_graph_execute_no_crash() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 256, 256, 512u);

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));

    rg.ClearSubmitted();
    rg.Execute(&dev);
}

}  // namespace

int main() {
    test_setup_render_graph_resolution_and_dag();
    test_setup_render_graph_custom_shadow_size();
    test_setup_render_graph_execute_no_crash();
    std::cout << "test_setup_render_graph OK\n";
    return 0;
}
