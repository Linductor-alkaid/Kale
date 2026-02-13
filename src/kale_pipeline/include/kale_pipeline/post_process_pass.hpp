/**
 * @file post_process_pass.hpp
 * @brief Post-Process Pass：声明 FinalColor 纹理，ReadTexture(Lighting)，WriteColor(FinalColor)
 *
 * 与 rendering_pipeline_layer_design.md 2.1、phase8-8.7 对齐。
 * 依赖 Lighting Pass 完成（通过 ReadTexture("Lighting") 声明依赖）。
 * Execute：占位实现（无绘制，依赖 Render Pass 的 load op 清屏）；
 * 完整 Bloom、Tone Mapping、FXAA 需 ShaderCompiler（phase8-8.3）提供着色器后接入。
 */

#pragma once

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/render_pass_builder.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>

namespace kale::pipeline {

/**
 * 执行 Post-Process Pass：当前为占位实现（无绘制，依赖 Render Pass 的 load op 清屏）。
 * 完整 Bloom、Tone Mapping、FXAA 需在 phase8-8.3 ShaderCompiler 就绪后接入着色器与 Pipeline。
 */
inline void ExecutePostProcessPass(const RenderPassContext& /*ctx*/, kale_device::CommandList& /*cmd*/) {
    // 占位：全屏三角形 + Bloom/ToneMapping/FXAA 需 ShaderCompiler 提供着色器后在此 BindPipeline / BindDescriptorSet / Draw(3)
}

/**
 * 向 RenderGraph 添加 Post-Process Pass。
 * 声明 DeclareTexture("FinalColor", RGBA8)，
 * Setup 中 ReadTexture(lightingResult)、WriteColor(0, finalColor)。
 * 依赖 Lighting Pass（通过 GetHandleByName("Lighting") 获取句柄）。
 * 调用顺序建议：SetResolution → SetupShadowPass → SetupGBufferPass → SetupLightingPass → SetupPostProcessPass → Compile。
 */
inline void SetupPostProcessPass(RenderGraph& rg) {
    using namespace kale_device;

    TextureDesc finalColorDesc;
    finalColorDesc.width = 0;
    finalColorDesc.height = 0;
    finalColorDesc.format = Format::RGBA8_UNORM;
    finalColorDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::Transfer;

    RGResourceHandle finalColor = rg.DeclareTexture("FinalColor", finalColorDesc);

    RGResourceHandle lightingResult = rg.GetHandleByName("Lighting");

    rg.AddPass(
        "PostProcess",
        [finalColor, lightingResult](RenderPassBuilder& b) {
            b.WriteColor(0, finalColor);
            if (lightingResult != kInvalidRGResourceHandle)
                b.ReadTexture(lightingResult);
        },
        [](const RenderPassContext& ctx, kale_device::CommandList& cmd) {
            ExecutePostProcessPass(ctx, cmd);
        });
}

}  // namespace kale::pipeline
