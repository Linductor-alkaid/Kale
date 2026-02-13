/**
 * @file test_gbuffer_pass.cpp
 * @brief phase8-8.5 GBuffer Pass 单元测试
 *
 * 覆盖：SetupGBufferPass 声明 GBufferAlbedo/GBufferNormal/GBufferDepth、
 * WriteColor(0,1)、WriteDepth、ReadTexture(ShadowMap)；Compile 成功；
 * 拓扑序 Shadow → GBuffer；Execute 不崩溃；无 Shadow 时单 Pass 也可 Compile。
 */

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/shadow_pass.hpp>
#include <kale_pipeline/gbuffer_pass.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_scene/scene_types.hpp>

#include <cstdlib>
#include <iostream>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
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

static void test_gbuffer_pass_setup_and_compile_with_shadow() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(800, 600);
    kale::pipeline::SetupShadowPass(rg, 2048u);
    kale::pipeline::SetupGBufferPass(rg);

    TEST_CHECK(rg.GetHandleByName("ShadowMap") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("GBufferAlbedo") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("GBufferNormal") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("GBufferDepth") != kale::pipeline::kInvalidRGResourceHandle);

    MockDevice dev;
    dev.Initialize({});
    bool ok = rg.Compile(&dev);
    TEST_CHECK(ok);
    TEST_CHECK(rg.IsCompiled());
    TEST_CHECK(rg.GetLastError().empty());

    const auto& order = rg.GetTopologicalOrder();
    TEST_CHECK(order.size() == 2u);

    const auto& passInfo = rg.GetCompiledPassInfo();
    TEST_CHECK(passInfo.size() == 2u);
    std::size_t gbufferPassIdx = 1u;
    const auto& gbufInfo = passInfo[gbufferPassIdx];
    TEST_CHECK(gbufInfo.colorOutputs.size() == 2u);
    TEST_CHECK(gbufInfo.depthOutput != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(!gbufInfo.writesSwapchain);
    TEST_CHECK(!gbufInfo.readTextures.empty());

    kale_device::TextureHandle albedoTex = rg.GetCompiledTexture(rg.GetHandleByName("GBufferAlbedo"));
    kale_device::TextureHandle normalTex = rg.GetCompiledTexture(rg.GetHandleByName("GBufferNormal"));
    kale_device::TextureHandle depthTex = rg.GetCompiledTexture(rg.GetHandleByName("GBufferDepth"));
    TEST_CHECK(albedoTex.IsValid());
    TEST_CHECK(normalTex.IsValid());
    TEST_CHECK(depthTex.IsValid());
}

static void test_gbuffer_pass_without_shadow() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(640, 480);
    kale::pipeline::SetupGBufferPass(rg);

    MockDevice dev;
    dev.Initialize({});
    bool ok = rg.Compile(&dev);
    TEST_CHECK(ok);
    TEST_CHECK(rg.IsCompiled());

    const auto& order = rg.GetTopologicalOrder();
    TEST_CHECK(order.size() == 1u);
}

static void test_gbuffer_pass_execute_no_crash() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(256, 256);
    kale::pipeline::SetupShadowPass(rg, 512u);
    kale::pipeline::SetupGBufferPass(rg);

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));

    rg.ClearSubmitted();
    rg.Execute(&dev);
}

}  // namespace

int main() {
    test_gbuffer_pass_setup_and_compile_with_shadow();
    test_gbuffer_pass_without_shadow();
    test_gbuffer_pass_execute_no_crash();
    std::cout << "test_gbuffer_pass OK\n";
    return 0;
}
