/**
 * @file transparent_pass.hpp
 * @brief Transparent Pass：依赖 Lighting 结果，向混合目标绘制透明物体
 *
 * 与 rendering_pipeline_layer_design.md、phase10-10.7 对齐。
 * 依赖 Lighting Pass 完成（ReadTexture(Lighting)）；写入同一 Lighting 纹理以实现叠加混合。
 * Execute：遍历 GetDrawsForPass(PassFlags::Transparent)，按深度从远到近排序后绘制。
 * Alpha 混合由透明材质的 Pipeline BlendState 提供，本 Pass 仅负责顺序与绘制。
 */

#pragma once

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/render_pass_builder.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_scene/scene_types.hpp>
#include <algorithm>
#include <vector>

namespace kale::pipeline {

/**
 * 执行 Transparent Pass：取 PassFlags::Transparent 的绘制项，按视空间深度从远到近排序后依次绘制。
 * 排序使用 worldTransform[3].z 作为深度代理（相机沿 -Z 时，z 大表示更远）。
 * 透明材质的 Alpha 混合由 Material/Pipeline 的 BlendState 提供。
 */
inline void ExecuteTransparentPass(const RenderPassContext& ctx, kale_device::CommandList& cmd) {
    std::vector<SubmittedDraw> draws = ctx.GetDrawsForPass(kale::scene::PassFlags::Transparent);
    if (draws.empty()) return;

    // 按深度从远到近排序：worldTransform[3].z 越大越远，先绘制远的再绘制近的
    std::sort(draws.begin(), draws.end(), [](const SubmittedDraw& a, const SubmittedDraw& b) {
        float za = a.worldTransform[3][2];
        float zb = b.worldTransform[3][2];
        return za > zb;
    });

    kale_device::IRenderDevice* device = ctx.GetDevice();
    for (const auto& draw : draws) {
        if (draw.renderable)
            draw.renderable->Draw(cmd, draw.worldTransform, device);
    }
}

/**
 * 向 RenderGraph 添加 Transparent Pass。
 * 依赖 Lighting（ReadTexture(Lighting)），写入 Lighting（WriteColor(0, Lighting)）作为混合目标。
 * 透明物体绘制在 Lighting 结果之上；后端应对本 Pass 的 color 附件使用 LOAD_OP_LOAD 以保留 Lighting 内容。
 * 调用顺序建议：SetResolution → SetupShadowPass → SetupGBufferPass → SetupLightingPass
 * → SetupTransparentPass → SetupPostProcessPass → SetupOutputToSwapchainPass → Compile。
 */
inline void SetupTransparentPass(RenderGraph& rg) {
    RGResourceHandle lightingResult = rg.GetHandleByName("Lighting");
    if (lightingResult == kInvalidRGResourceHandle) return;

    rg.AddPass(
        "TransparentPass",
        [lightingResult](RenderPassBuilder& b) {
            b.ReadTexture(lightingResult);
            b.WriteColor(0, lightingResult);
        },
        [](const RenderPassContext& ctx, kale_device::CommandList& cmd) {
            ExecuteTransparentPass(ctx, cmd);
        });
}

}  // namespace kale::pipeline
