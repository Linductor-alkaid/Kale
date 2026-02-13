/**
 * @file test_multi_shadow_passes.cpp
 * @brief phase12-12.7 多光源 Shadow Pass 单元测试
 *
 * 覆盖：AddShadowPass 单次添加命名 Pass/ShadowMap；SetupMultiShadowPasses 添加 N 个 Pass；
 * 多个 Shadow Pass 同拓扑组可并行；Execute 不崩溃；GetHandleByName(ShadowMap0/1) 有效。
 */

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/shadow_pass.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>

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

static void test_add_shadow_pass_named() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(512, 512);
    kale::pipeline::AddShadowPass(rg, "ShadowPassDir", "ShadowMapDir", 1024u);

    TEST_CHECK(rg.GetHandleByName("ShadowMapDir") != kale::pipeline::kInvalidRGResourceHandle);

    MockDevice dev;
    dev.Initialize({});
    bool ok = rg.Compile(&dev);
    TEST_CHECK(ok);
    TEST_CHECK(rg.IsCompiled());

    const auto& order = rg.GetTopologicalOrder();
    TEST_CHECK(order.size() == 1u);

    const auto& groups = rg.GetTopologicalGroups();
    TEST_CHECK(groups.size() == 1u);
    TEST_CHECK(groups[0].size() == 1u);
}

static void test_setup_multi_shadow_passes() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(800, 600);
    kale::pipeline::SetupMultiShadowPasses(rg, 512u, 3u);

    TEST_CHECK(rg.GetHandleByName("ShadowMap0") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("ShadowMap1") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("ShadowMap2") != kale::pipeline::kInvalidRGResourceHandle);

    MockDevice dev;
    dev.Initialize({});
    bool ok = rg.Compile(&dev);
    TEST_CHECK(ok);

    const auto& order = rg.GetTopologicalOrder();
    TEST_CHECK(order.size() == 3u);

    // 多个 Shadow Pass 无依赖，应在同一拓扑组内（可并行录制）
    const auto& groups = rg.GetTopologicalGroups();
    TEST_CHECK(groups.size() == 1u);
    TEST_CHECK(groups[0].size() == 3u);
}

static void test_multi_shadow_execute_no_crash() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(256, 256);
    kale::pipeline::SetupMultiShadowPasses(rg, 256u, 2u);

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));

    rg.ClearSubmitted();
    rg.Execute(&dev);
}

static void test_setup_shadow_pass_still_single() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(256, 256);
    kale::pipeline::SetupShadowPass(rg, 1024u);

    TEST_CHECK(rg.GetHandleByName("ShadowMap") != kale::pipeline::kInvalidRGResourceHandle);

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));

    const auto& order = rg.GetTopologicalOrder();
    TEST_CHECK(order.size() == 1u);
}

}  // namespace

int main() {
    test_add_shadow_pass_named();
    test_setup_multi_shadow_passes();
    test_multi_shadow_execute_no_crash();
    test_setup_shadow_pass_still_single();
    std::cout << "test_multi_shadow_passes OK\n";
    return 0;
}
