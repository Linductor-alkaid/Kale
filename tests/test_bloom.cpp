/**
 * @file test_bloom.cpp
 * @brief phase14-14.2 Bloom 单元测试
 *
 * 覆盖：SetBloomEnabled/IsBloomEnabled；SetBloomThreshold/GetBloomThreshold；SetBloomStrength/GetBloomStrength；
 * SetupPostProcessPass 启用 Bloom 时声明 BloomBright/BloomBlurA/BloomBlurB 并添加 ExtractBrightness、BloomBlurH、BloomBlurV、PostProcess；
 * ExecuteExtractBrightnessPass、ExecuteBloomBlurHPass、ExecuteBloomBlurVPass 在 device 空或句柄无效时不崩溃。
 */

#include <kale_pipeline/post_process_pass.hpp>
#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_pipeline/setup_render_graph.hpp>
#include <kale_pipeline/shadow_pass.hpp>
#include <kale_pipeline/gbuffer_pass.hpp>
#include <kale_pipeline/lighting_pass.hpp>
#include <kale_pipeline/transparent_pass.hpp>
#include <kale_pipeline/output_to_swapchain_pass.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

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
    void CopyBufferToBuffer(kale_device::BufferHandle, std::size_t, kale_device::BufferHandle,
                            std::size_t, std::size_t) override {}
    void CopyBufferToTexture(kale_device::BufferHandle, std::size_t, kale_device::TextureHandle,
                             std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void CopyTextureToTexture(kale_device::TextureHandle, kale_device::TextureHandle,
                              std::uint32_t, std::uint32_t) override {}
    void Barrier(const std::vector<kale_device::TextureHandle>&) override {}
    void ClearColor(kale_device::TextureHandle, const float[4]) override {}
    void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
};

static void test_bloom_api() {
    TEST_CHECK(!kale::pipeline::IsBloomEnabled());
    kale::pipeline::SetBloomEnabled(true);
    TEST_CHECK(kale::pipeline::IsBloomEnabled());
    kale::pipeline::SetBloomEnabled(false);
    TEST_CHECK(!kale::pipeline::IsBloomEnabled());

    kale::pipeline::SetBloomThreshold(0.8f);
    TEST_CHECK(kale::pipeline::GetBloomThreshold() == 0.8f);
    kale::pipeline::SetBloomStrength(0.1f);
    TEST_CHECK(kale::pipeline::GetBloomStrength() == 0.1f);
    kale::pipeline::SetBloomThreshold(1.0f);
    kale::pipeline::SetBloomStrength(0.04f);
}

static void test_setup_post_process_with_bloom_adds_passes() {
    kale::pipeline::SetBloomEnabled(true);
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale::pipeline::SetupShadowPass(rg, 64);
    kale::pipeline::SetupGBufferPass(rg);
    kale::pipeline::SetupLightingPass(rg);
    kale::pipeline::SetupTransparentPass(rg);
    kale::pipeline::SetupPostProcessPass(rg);
    kale::pipeline::SetupOutputToSwapchainPass(rg);

    TEST_CHECK(rg.GetHandleByName("BloomBright") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("BloomBlurA") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("BloomBlurB") != kale::pipeline::kInvalidRGResourceHandle);

    kale_device::IRenderDevice* dev = nullptr;
    (void)dev;
    bool compiled = rg.Compile(dev);
    if (compiled) {
        const auto& passes = rg.GetPasses();
        auto order = rg.GetTopologicalOrder();
        bool hasExtract = false, hasBlurH = false, hasBlurV = false, hasPost = false;
        for (auto h : order) {
            size_t i = static_cast<size_t>(h);
            if (i < passes.size()) {
                const std::string& n = passes[i].name;
                if (n == "ExtractBrightness") hasExtract = true;
                if (n == "BloomBlurH") hasBlurH = true;
                if (n == "BloomBlurV") hasBlurV = true;
                if (n == "PostProcess") hasPost = true;
            }
        }
        TEST_CHECK(hasExtract && hasBlurH && hasBlurV && hasPost);
    }
    kale::pipeline::SetBloomEnabled(false);
}

static void test_execute_bloom_passes_no_crash() {
    std::vector<kale::pipeline::SubmittedDraw> draws;
    kale::pipeline::RenderPassContext ctx(&draws, nullptr, nullptr);
    MockCommandList cmd;

    kale::pipeline::ExecuteExtractBrightnessPass(ctx, cmd, kale::pipeline::kInvalidRGResourceHandle);
    kale::pipeline::ExecuteExtractBrightnessPass(ctx, cmd, 1u);

    kale::pipeline::ExecuteBloomBlurHPass(ctx, cmd, kale::pipeline::kInvalidRGResourceHandle);
    kale::pipeline::ExecuteBloomBlurHPass(ctx, cmd, 1u);

    kale::pipeline::ExecuteBloomBlurVPass(ctx, cmd, kale::pipeline::kInvalidRGResourceHandle);
    kale::pipeline::ExecuteBloomBlurVPass(ctx, cmd, 1u);
}

static void test_post_process_without_bloom_single_pass() {
    kale::pipeline::SetBloomEnabled(false);
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale::pipeline::SetupPostProcessPass(rg);
    TEST_CHECK(rg.GetPasses().size() == 1u);
    TEST_CHECK(rg.GetHandleByName("BloomBright") == kale::pipeline::kInvalidRGResourceHandle);
}

}  // namespace

int main() {
    test_bloom_api();
    test_setup_post_process_with_bloom_adds_passes();
    test_execute_bloom_passes_no_crash();
    test_post_process_without_bloom_single_pass();
    std::cout << "test_bloom: all passed\n";
    return 0;
}
