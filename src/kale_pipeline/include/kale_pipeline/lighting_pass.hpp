/**
 * @file lighting_pass.hpp
 * @brief Lighting Pass：声明 Lighting 结果纹理，ReadTexture(GBuffer + ShadowMap)，WriteColor(Lighting)
 *
 * 与 rendering_pipeline_layer_design.md 5.9、phase8-8.6 对齐。
 * 依赖 GBuffer Pass 与 Shadow Pass 完成（通过 ReadTexture 声明依赖）。
 * Execute：全屏绘制占位（ClearColor）；完整 PBR 光照计算需 ShaderCompiler（phase8-8.3）提供着色器后接入。
 */

#pragma once

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/render_pass_builder.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>

namespace kale::pipeline {

/**
 * 执行 Lighting Pass：当前为占位实现（无绘制，依赖 Render Pass 的 load op 清屏）。
 * 完整 PBR 全屏三角形 + 光照 UBO 需在 phase8-8.3 ShaderCompiler 就绪后接入着色器与 Pipeline。
 */
inline void ExecuteLightingPass(const RenderPassContext& /*ctx*/, kale_device::CommandList& /*cmd*/) {
    // 占位：全屏三角形 + PBR 光照需 ShaderCompiler 提供着色器后在此 BindPipeline / BindDescriptorSet / Draw(3)
}

/**
 * 向 RenderGraph 添加 Lighting Pass。
 * 声明 DeclareTexture("Lighting", RGBA16F)，
 * Setup 中 ReadTexture(gbufferAlbedo, gbufferNormal, gbufferDepth, shadowMap)、WriteColor(0, lightingResult)。
 * 依赖 GBuffer Pass 与 Shadow Pass（通过 GetHandleByName 获取句柄，未声明则跳过对应 ReadTexture）。
 * 调用顺序建议：SetResolution → SetupShadowPass → SetupGBufferPass → SetupLightingPass → Compile。
 */
inline void SetupLightingPass(RenderGraph& rg) {
    using namespace kale_device;

    TextureDesc lightingDesc;
    lightingDesc.width = 0;
    lightingDesc.height = 0;
    lightingDesc.format = Format::RGBA16F;
    lightingDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;

    RGResourceHandle lightingResult = rg.DeclareTexture("Lighting", lightingDesc);

    RGResourceHandle gbufferAlbedo = rg.GetHandleByName("GBufferAlbedo");
    RGResourceHandle gbufferNormal = rg.GetHandleByName("GBufferNormal");
    RGResourceHandle gbufferDepth = rg.GetHandleByName("GBufferDepth");
    RGResourceHandle shadowMap = rg.GetHandleByName("ShadowMap");

    rg.AddPass(
        "LightingPass",
        [lightingResult, gbufferAlbedo, gbufferNormal, gbufferDepth, shadowMap](RenderPassBuilder& b) {
            b.WriteColor(0, lightingResult);
            if (gbufferAlbedo != kInvalidRGResourceHandle) b.ReadTexture(gbufferAlbedo);
            if (gbufferNormal != kInvalidRGResourceHandle) b.ReadTexture(gbufferNormal);
            if (gbufferDepth != kInvalidRGResourceHandle) b.ReadTexture(gbufferDepth);
            if (shadowMap != kInvalidRGResourceHandle) b.ReadTexture(shadowMap);
        },
        [](const RenderPassContext& ctx, kale_device::CommandList& cmd) {
            ExecuteLightingPass(ctx, cmd);
        });
}

}  // namespace kale::pipeline
