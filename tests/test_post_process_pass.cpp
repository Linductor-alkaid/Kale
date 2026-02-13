/**
 * @file test_post_process_pass.cpp
 * @brief phase8-8.7 Post-Process Pass 单元测试
 *
 * 覆盖：SetupPostProcessPass 声明 FinalColor、ReadTexture(Lighting)、WriteColor(0)；
 * Compile 成功；拓扑序 Shadow → GBuffer → Lighting → PostProcess；Execute 不崩溃；
 * 仅 SetupPostProcessPass 时单 Pass Compile。
 */

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/shadow_pass.hpp>
#include <kale_pipeline/gbuffer_pass.hpp>
#include <kale_pipeline/lighting_pass.hpp>
#include <kale_pipeline/post_process_pass.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_device/render_device.hpp>
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

    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return nullptr; }
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

    std::uint32_t AcquireNextImage() override { return kale_device::IRenderDevice::kInvalidSwapchainImageIndex; }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }
    void SetExtent(std::uint32_t, std::uint32_t) override {}

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextTexId_ = 0;
    std::uint64_t nextFenceId_ = 0;
};

static void test_post_process_pass_setup_and_compile_full_chain() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(800, 600);
    kale::pipeline::SetupShadowPass(rg, 2048u);
    kale::pipeline::SetupGBufferPass(rg);
    kale::pipeline::SetupLightingPass(rg);
    kale::pipeline::SetupPostProcessPass(rg);

    TEST_CHECK(rg.GetHandleByName("FinalColor") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("Lighting") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("GBufferAlbedo") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("ShadowMap") != kale::pipeline::kInvalidRGResourceHandle);

    MockDevice dev;
    dev.Initialize({});
    bool ok = rg.Compile(&dev);
    TEST_CHECK(ok);
    TEST_CHECK(rg.IsCompiled());
    TEST_CHECK(rg.GetLastError().empty());

    const auto& order = rg.GetTopologicalOrder();
    TEST_CHECK(order.size() == 4u);  // Shadow → GBuffer → Lighting → PostProcess

    const auto& passInfo = rg.GetCompiledPassInfo();
    TEST_CHECK(passInfo.size() == 4u);
    std::size_t postProcessPassIdx = 3u;
    const auto& ppInfo = passInfo[postProcessPassIdx];
    TEST_CHECK(ppInfo.colorOutputs.size() == 1u);
    TEST_CHECK(ppInfo.depthOutput == kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(!ppInfo.writesSwapchain);
    TEST_CHECK(ppInfo.readTextures.size() == 1u);  // Lighting

    kale_device::TextureHandle finalColorTex = rg.GetCompiledTexture(rg.GetHandleByName("FinalColor"));
    TEST_CHECK(finalColorTex.IsValid());
}

static void test_post_process_pass_execute_no_crash() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(256, 256);
    kale::pipeline::SetupShadowPass(rg, 512u);
    kale::pipeline::SetupGBufferPass(rg);
    kale::pipeline::SetupLightingPass(rg);
    kale::pipeline::SetupPostProcessPass(rg);

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));

    rg.ClearSubmitted();
    rg.Execute(&dev);
}

static void test_post_process_pass_only() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(640, 480);
    kale::pipeline::SetupPostProcessPass(rg);

    TEST_CHECK(rg.GetHandleByName("FinalColor") != kale::pipeline::kInvalidRGResourceHandle);

    MockDevice dev;
    dev.Initialize({});
    bool ok = rg.Compile(&dev);
    TEST_CHECK(ok);
    TEST_CHECK(rg.IsCompiled());

    const auto& order = rg.GetTopologicalOrder();
    TEST_CHECK(order.size() == 1u);

    const auto& passInfo = rg.GetCompiledPassInfo();
    TEST_CHECK(passInfo.size() == 1u);
    TEST_CHECK(passInfo[0].readTextures.empty());
    TEST_CHECK(passInfo[0].colorOutputs.size() == 1u);
}

}  // namespace

int main() {
    test_post_process_pass_setup_and_compile_full_chain();
    test_post_process_pass_execute_no_crash();
    test_post_process_pass_only();
    std::cout << "test_post_process_pass OK\n";
    return 0;
}
