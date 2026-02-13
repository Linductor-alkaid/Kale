/**
 * @file shadow_pass.hpp
 * @brief Shadow Pass：声明 ShadowMap、WriteDepth，遍历 GetDrawsForPass(ShadowCaster) 绘制
 *
 * 与 rendering_pipeline_layer_design.md 5.9、phase8-8.4 对齐。
 * Shadow Pass 无前置依赖，可最早执行。
 * Shadow 相机矩阵（正交投影）可由应用层通过 ShadowViewProjMatrix 生成并用于自定义 execute。
 */

#pragma once

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_scene/scene_types.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace kale::pipeline {

/** 默认 ShadowMap 尺寸 */
constexpr std::uint32_t kDefaultShadowMapSize = 2048u;

/**
 * 执行 Shadow Pass 的绘制逻辑：对 GetDrawsForPass(PassFlags::ShadowCaster) 中每项调用
 * renderable->Draw(cmd, worldTransform, device)。供 AddPass 的 execute 回调使用。
 */
inline void ExecuteShadowPass(const RenderPassContext& ctx, kale_device::CommandList& cmd) {
    auto draws = ctx.GetDrawsForPass(kale::scene::PassFlags::ShadowCaster);
    for (const auto& draw : draws) {
        if (draw.renderable)
            draw.renderable->Draw(cmd, draw.worldTransform, ctx.GetDevice());
    }
}

/**
 * 生成阴影用正交投影矩阵（用于 Shadow 相机）。
 * @param halfSize 正交投影半边长（世界单位）
 * @param nearPlane 近平面
 * @param farPlane 远平面
 * @return 正交投影矩阵
 */
inline glm::mat4 ShadowOrthoProjectionMatrix(float halfSize, float nearPlane, float farPlane) {
    return glm::ortho(-halfSize, halfSize, -halfSize, halfSize, nearPlane, farPlane);
}

/**
 * 向 RenderGraph 添加 Shadow Pass（仅写深度到 ShadowMap，无颜色附件）。
 * 声明 DeclareTexture("ShadowMap", {size, size, Format::D32, DepthAttachment})，
 * AddPass("ShadowPass", setup=WriteDepth(shadowMap), execute=ExecuteShadowPass)。
 * Shadow Pass 无前置依赖，可最早执行。
 * @param rg 已 SetResolution 的 RenderGraph
 * @param shadowMapSize ShadowMap 宽高，默认 2048
 */
inline void SetupShadowPass(RenderGraph& rg, std::uint32_t shadowMapSize = kDefaultShadowMapSize) {
    kale_device::TextureDesc shadowDesc;
    shadowDesc.width = shadowMapSize;
    shadowDesc.height = shadowMapSize;
    shadowDesc.format = kale_device::Format::D32;
    shadowDesc.usage = kale_device::TextureUsage::DepthAttachment;

    RGResourceHandle shadowMap = rg.DeclareTexture("ShadowMap", shadowDesc);

    rg.AddPass(
        "ShadowPass",
        [shadowMap](RenderPassBuilder& b) {
            b.WriteDepth(shadowMap);
        },
        [](const RenderPassContext& ctx, kale_device::CommandList& cmd) {
            ExecuteShadowPass(ctx, cmd);
        });
}

}  // namespace kale::pipeline
