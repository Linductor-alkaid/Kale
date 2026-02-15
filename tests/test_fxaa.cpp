/**
 * @file test_fxaa.cpp
 * @brief phase14-14.3 FXAA 单元测试
 *
 * 覆盖：SetFXAAEnabled/IsFXAAEnabled；SetFXAAQuality/GetFXAAQuality；
 * SetupPostProcessPass 启用 FXAA 时声明 PostProcessOutput 并添加 FXAA Pass；
 * ExecuteFXAAPass 在 device 空、句柄无效时不崩溃；禁用 FXAA 时单 PostProcess Pass 且无 PostProcessOutput。
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

static void test_fxaa_api() {
    TEST_CHECK(!kale::pipeline::IsFXAAEnabled());
    kale::pipeline::SetFXAAEnabled(true);
    TEST_CHECK(kale::pipeline::IsFXAAEnabled());
    kale::pipeline::SetFXAAEnabled(false);
    TEST_CHECK(!kale::pipeline::IsFXAAEnabled());

    TEST_CHECK(kale::pipeline::GetFXAAQuality() == 1);
    kale::pipeline::SetFXAAQuality(0);
    TEST_CHECK(kale::pipeline::GetFXAAQuality() == 0);
    kale::pipeline::SetFXAAQuality(2);
    TEST_CHECK(kale::pipeline::GetFXAAQuality() == 2);
    kale::pipeline::SetFXAAQuality(-1);
    TEST_CHECK(kale::pipeline::GetFXAAQuality() == 1);
    kale::pipeline::SetFXAAQuality(99);
    TEST_CHECK(kale::pipeline::GetFXAAQuality() == 1);
    kale::pipeline::SetFXAAQuality(1);
}

static void test_setup_post_process_with_fxaa_adds_pass() {
    kale::pipeline::SetBloomEnabled(false);
    kale::pipeline::SetFXAAEnabled(true);
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale::pipeline::SetupShadowPass(rg, 64);
    kale::pipeline::SetupGBufferPass(rg);
    kale::pipeline::SetupLightingPass(rg);
    kale::pipeline::SetupTransparentPass(rg);
    kale::pipeline::SetupPostProcessPass(rg);
    kale::pipeline::SetupOutputToSwapchainPass(rg);

    TEST_CHECK(rg.GetHandleByName("PostProcessOutput") != kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(rg.GetHandleByName("FinalColor") != kale::pipeline::kInvalidRGResourceHandle);

    kale_device::IRenderDevice* dev = nullptr;
    bool compiled = rg.Compile(dev);
    if (compiled) {
        const auto& passes = rg.GetPasses();
        auto order = rg.GetTopologicalOrder();
        bool hasPost = false, hasFXAA = false;
        for (auto h : order) {
            size_t i = static_cast<size_t>(h);
            if (i < passes.size()) {
                const std::string& n = passes[i].name;
                if (n == "PostProcess") hasPost = true;
                if (n == "FXAA") hasFXAA = true;
            }
        }
        TEST_CHECK(hasPost && hasFXAA);
    }
    kale::pipeline::SetFXAAEnabled(false);
}

static void test_execute_fxaa_pass_no_crash() {
    std::vector<kale::pipeline::SubmittedDraw> draws;
    kale::pipeline::RenderPassContext ctx(&draws, nullptr, nullptr);
    MockCommandList cmd;

    kale::pipeline::ExecuteFXAAPass(ctx, cmd, kale::pipeline::kInvalidRGResourceHandle);
    kale::pipeline::ExecuteFXAAPass(ctx, cmd, 1u);
}

static void test_post_process_without_fxaa_single_pass() {
    kale::pipeline::SetBloomEnabled(false);
    kale::pipeline::SetFXAAEnabled(false);
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale::pipeline::SetupPostProcessPass(rg);
    TEST_CHECK(rg.GetPasses().size() == 1u);
    TEST_CHECK(rg.GetHandleByName("PostProcessOutput") == kale::pipeline::kInvalidRGResourceHandle);
}

}  // namespace

int main() {
    test_fxaa_api();
    test_setup_post_process_with_fxaa_adds_pass();
    test_execute_fxaa_pass_no_crash();
    test_post_process_without_fxaa_single_pass();
    std::cout << "test_fxaa: all passed\n";
    return 0;
}
