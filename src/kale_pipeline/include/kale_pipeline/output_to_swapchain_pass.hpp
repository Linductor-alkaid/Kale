/**
 * @file output_to_swapchain_pass.hpp
 * @brief OutputToSwapchain Pass：ReadTexture(FinalColor)、WriteSwapchain，Execute 中 Copy finalColor → BackBuffer
 *
 * 与 rendering_pipeline_layer_design.md 3.5、phase8-8.8 对齐。
 * 依赖 PostProcess Pass 完成（通过 ReadTexture("FinalColor") 声明依赖）。
 * 本 Pass 使用 SetExecuteWithoutRenderPass，不进入 Render Pass，仅执行 CopyTextureToTexture。
 */

#pragma once

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/render_pass_builder.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/render_device.hpp>

namespace kale::pipeline {

/**
 * 执行 OutputToSwapchain Pass：将 FinalColor 拷贝到当前 BackBuffer。
 * 要求 RenderPassContext 已绑定 RenderGraph（GetCompiledTexture 可用），device 非空。
 */
inline void ExecuteOutputToSwapchainPass(const RenderPassContext& ctx,
                                         kale_device::CommandList& cmd,
                                         RGResourceHandle finalColorHandle,
                                         std::uint32_t copyWidth,
                                         std::uint32_t copyHeight) {
    kale_device::IRenderDevice* device = ctx.GetDevice();
    if (!device) return;
    kale_device::TextureHandle srcTex = ctx.GetCompiledTexture(finalColorHandle);
    kale_device::TextureHandle dstTex = device->GetBackBuffer();
    if (!srcTex.IsValid() || !dstTex.IsValid() || copyWidth == 0 || copyHeight == 0) return;
    cmd.CopyTextureToTexture(srcTex, dstTex, copyWidth, copyHeight);
}

/**
 * 向 RenderGraph 添加 OutputToSwapchain Pass。
 * Setup 中 ReadTexture(finalColor)、WriteSwapchain()、SetExecuteWithoutRenderPass(true)。
 * Execute 中调用 CopyTextureToTexture(finalColor → GetBackBuffer())。
 * 调用顺序建议：SetResolution → SetupShadowPass → SetupGBufferPass → SetupLightingPass
 * → SetupPostProcessPass → SetupOutputToSwapchainPass → Compile。
 */
inline void SetupOutputToSwapchainPass(RenderGraph& rg) {
    using namespace kale_device;

    RGResourceHandle finalColor = rg.GetHandleByName("FinalColor");
    if (finalColor == kInvalidRGResourceHandle) return;

    std::uint32_t copyW = rg.GetResolutionWidth();
    std::uint32_t copyH = rg.GetResolutionHeight();
    if (copyW == 0) copyW = 1920;
    if (copyH == 0) copyH = 1080;

    rg.AddPass(
        "OutputToSwapchain",
        [finalColor](RenderPassBuilder& b) {
            b.ReadTexture(finalColor);
            b.WriteSwapchain();
            b.SetExecuteWithoutRenderPass(true);
        },
        [finalColor, copyW, copyH](const RenderPassContext& ctx, CommandList& cmd) {
            ExecuteOutputToSwapchainPass(ctx, cmd, finalColor, copyW, copyH);
        });
}

}  // namespace kale::pipeline
