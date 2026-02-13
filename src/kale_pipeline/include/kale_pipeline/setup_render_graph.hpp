/**
 * @file setup_render_graph.hpp
 * @brief 完整 Deferred + Shadow 管线示例：SetupRenderGraph 一键配置
 *
 * 与 rendering_pipeline_layer_design.md、phase8-8.9 对齐。
 * 按顺序：SetResolution → SetupShadowPass → SetupGBufferPass → SetupLightingPass
 * → SetupPostProcessPass → SetupOutputToSwapchainPass。
 * 应用层在调用 SetupRenderGraph 后调用 rg.Compile(device)。
 */

#pragma once

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/shadow_pass.hpp>
#include <kale_pipeline/gbuffer_pass.hpp>
#include <kale_pipeline/lighting_pass.hpp>
#include <kale_pipeline/post_process_pass.hpp>
#include <kale_pipeline/output_to_swapchain_pass.hpp>

namespace kale::pipeline {

/**
 * 为 RenderGraph 配置完整 Deferred + Shadow 管线。
 * 建立 Pass 依赖 DAG：Shadow → GBuffer → Lighting → PostProcess → OutputToSwapchain；
 * DeclareTexture 使用当前 SetResolution 的宽高（GBuffer/Lighting/FinalColor 等）。
 *
 * @param rg 待配置的 RenderGraph（本函数内会调用 SetResolution）
 * @param width 分辨率宽（用于 SetResolution 及所有 0×0 的 DeclareTexture）
 * @param height 分辨率高
 * @param shadowMapSize ShadowMap 纹理边长，默认 2048
 */
inline void SetupRenderGraph(RenderGraph& rg,
                            std::uint32_t width,
                            std::uint32_t height,
                            std::uint32_t shadowMapSize = kDefaultShadowMapSize) {
    rg.SetResolution(width, height);
    SetupShadowPass(rg, shadowMapSize);
    SetupGBufferPass(rg);
    SetupLightingPass(rg);
    SetupPostProcessPass(rg);
    SetupOutputToSwapchainPass(rg);
}

}  // namespace kale::pipeline
