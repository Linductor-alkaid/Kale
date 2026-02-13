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
 * 向 RenderGraph 添加单个命名 Shadow Pass（仅写深度到指定名称的 ShadowMap，无颜色附件）。
 * 用于多光源场景：可多次调用以添加多个 Shadow Pass，每个写入独立 ShadowMap；
 * 这些 Pass 无前置依赖，会落在同一拓扑组内，可并行录制（phase12-12.7）。
 * @param rg 已 SetResolution 的 RenderGraph
 * @param passName Pass 名称（如 "ShadowPass0"）
 * @param shadowMapName 声明的纹理名称（如 "ShadowMap0"），供后续 GBuffer/Lighting ReadTexture 使用
 * @param shadowMapSize ShadowMap 宽高
 */
inline void AddShadowPass(RenderGraph& rg,
                          const std::string& passName,
                          const std::string& shadowMapName,
                          std::uint32_t shadowMapSize) {
    kale_device::TextureDesc shadowDesc;
    shadowDesc.width = shadowMapSize;
    shadowDesc.height = shadowMapSize;
    shadowDesc.format = kale_device::Format::D32;
    shadowDesc.usage = kale_device::TextureUsage::DepthAttachment;

    RGResourceHandle shadowMap = rg.DeclareTexture(shadowMapName, shadowDesc);

    rg.AddPass(
        passName,
        [shadowMap](RenderPassBuilder& b) {
            b.WriteDepth(shadowMap);
        },
        [](const RenderPassContext& ctx, kale_device::CommandList& cmd) {
            ExecuteShadowPass(ctx, cmd);
        });
}

/**
 * 向 RenderGraph 添加多组 Shadow Pass（多光源 Shadow Pass，phase12-12.7）。
 * 添加 numPasses 个 Shadow Pass，名称分别为 ShadowPass0, ShadowPass1, ...，
 * 对应 ShadowMap0, ShadowMap1, ...；各 Pass 无依赖，同组内可并行录制。
 * @param rg 已 SetResolution 的 RenderGraph
 * @param shadowMapSize 每个 ShadowMap 的宽高
 * @param numPasses 光源/Shadow Pass 数量（如 2 表示 Directional + Point）
 */
inline void SetupMultiShadowPasses(RenderGraph& rg,
                                   std::uint32_t shadowMapSize,
                                   std::uint32_t numPasses) {
    for (std::uint32_t i = 0; i < numPasses; ++i) {
        std::string passName = "ShadowPass" + std::to_string(i);
        std::string shadowMapName = "ShadowMap" + std::to_string(i);
        AddShadowPass(rg, passName, shadowMapName, shadowMapSize);
    }
}

/**
 * 向 RenderGraph 添加单光源 Shadow Pass（仅写深度到 ShadowMap，无颜色附件）。
 * 声明 DeclareTexture("ShadowMap", {size, size, Format::D32, DepthAttachment})，
 * AddPass("ShadowPass", setup=WriteDepth(shadowMap), execute=ExecuteShadowPass)。
 * Shadow Pass 无前置依赖，可最早执行。
 * @param rg 已 SetResolution 的 RenderGraph
 * @param shadowMapSize ShadowMap 宽高，默认 2048
 */
inline void SetupShadowPass(RenderGraph& rg, std::uint32_t shadowMapSize = kDefaultShadowMapSize) {
    AddShadowPass(rg, "ShadowPass", "ShadowMap", shadowMapSize);
}

}  // namespace kale::pipeline
