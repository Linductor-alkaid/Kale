/**
 * @file post_process_pass.hpp
 * @brief Post-Process Pass：声明 FinalColor 纹理，ReadTexture(Lighting)，WriteColor(FinalColor)
 *
 * 与 rendering_pipeline_layer_design.md 2.1、phase8-8.7、phase14-14.1 对齐。
 * 依赖 Lighting Pass 完成（通过 ReadTexture("Lighting") 声明依赖）。
 * Execute：Tone Mapping（Reinhard + 可选曝光），全屏三角形 BindPipeline/BindDescriptorSet(Lighting)/Draw(3)。
 * 应用层需在 Compile 前调用 SetToneMappingShaderDirectory 指向含 tone_mapping.vert.spv / tone_mapping.frag.spv 的目录。
 */

#pragma once

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/render_pass_builder.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <string>

namespace kale::pipeline {

/**
 * 设置 Tone Mapping 着色器目录（含 tone_mapping.vert.spv、tone_mapping.frag.spv）。
 * 未设置或目录无效时 ExecutePostProcessPass 不绘制。
 */
void SetToneMappingShaderDirectory(const std::string& directory);

/**
 * 执行 Post-Process Pass：Tone Mapping（Reinhard，曝光 1.0），BindPipeline、BindDescriptorSet(Lighting)、Draw(3)。
 * lightingTextureHandle 为 Lighting 的 RGResourceHandle，由 SetupPostProcessPass 传入。
 */
void ExecutePostProcessPass(const RenderPassContext& ctx,
                            kale_device::CommandList& cmd,
                            RGResourceHandle lightingTextureHandle);

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
        [lightingResult](const RenderPassContext& ctx, kale_device::CommandList& cmd) {
            ExecutePostProcessPass(ctx, cmd, lightingResult);
        });
}

}  // namespace kale::pipeline
