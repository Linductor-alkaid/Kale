/**
 * @file gbuffer_pass.hpp
 * @brief GBuffer Pass：声明 Albedo/Normal/Depth，ReadTexture(ShadowMap)，遍历 GetDrawsForPass(Opaque) 绘制
 *
 * 与 rendering_pipeline_layer_design.md 2.2、phase8-8.5 对齐。
 * 依赖 Shadow Pass 完成（通过 ReadTexture(shadowMap) 声明依赖）。
 */

#pragma once

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/render_pass_builder.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_scene/scene_types.hpp>

namespace kale::pipeline {

/**
 * 执行 GBuffer Pass 的绘制逻辑：对 GetDrawsForPass(PassFlags::Opaque) 中每项调用
 * renderable->Draw(cmd, worldTransform, device)。供 AddPass 的 execute 回调使用。
 */
inline void ExecuteGBufferPass(const RenderPassContext& ctx, kale_device::CommandList& cmd) {
    auto draws = ctx.GetDrawsForPass(kale::scene::PassFlags::Opaque);
    for (const auto& draw : draws) {
        if (draw.renderable)
            draw.renderable->Draw(cmd, draw.worldTransform, ctx.GetDevice());
    }
}

/**
 * 向 RenderGraph 添加 GBuffer Pass。
 * 声明 Albedo（RGBA8）、Normal（RGBA16F）、Depth（D24S8），
 * Setup 中 WriteColor(0, albedo)、WriteColor(1, normal)、WriteDepth(depth)、ReadTexture(shadowMap)，
 * 依赖 Shadow Pass（若已通过 SetupShadowPass 声明 "ShadowMap"）。
 * 调用顺序建议：SetResolution → SetupShadowPass → SetupGBufferPass → Compile。
 * @param rg 已 SetResolution 的 RenderGraph；若已调用 SetupShadowPass，则 ReadTexture("ShadowMap") 建立依赖
 */
inline void SetupGBufferPass(RenderGraph& rg) {
    using namespace kale_device;

    TextureDesc albedoDesc;
    albedoDesc.width = 0;
    albedoDesc.height = 0;
    albedoDesc.format = Format::RGBA8_UNORM;
    albedoDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;

    TextureDesc normalDesc;
    normalDesc.width = 0;
    normalDesc.height = 0;
    normalDesc.format = Format::RGBA16F;
    normalDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;

    TextureDesc depthDesc;
    depthDesc.width = 0;
    depthDesc.height = 0;
    depthDesc.format = Format::D24S8;
    depthDesc.usage = TextureUsage::DepthAttachment | TextureUsage::Sampled;

    RGResourceHandle gbufferAlbedo = rg.DeclareTexture("GBufferAlbedo", albedoDesc);
    RGResourceHandle gbufferNormal = rg.DeclareTexture("GBufferNormal", normalDesc);
    RGResourceHandle gbufferDepth = rg.DeclareTexture("GBufferDepth", depthDesc);

    RGResourceHandle shadowMap = rg.GetHandleByName("ShadowMap");

    rg.AddPass(
        "GBufferPass",
        [gbufferAlbedo, gbufferNormal, gbufferDepth, shadowMap](RenderPassBuilder& b) {
            b.WriteColor(0, gbufferAlbedo);
            b.WriteColor(1, gbufferNormal);
            b.WriteDepth(gbufferDepth);
            if (shadowMap != kInvalidRGResourceHandle)
                b.ReadTexture(shadowMap);
        },
        [](const RenderPassContext& ctx, kale_device::CommandList& cmd) {
            ExecuteGBufferPass(ctx, cmd);
        });
}

}  // namespace kale::pipeline
