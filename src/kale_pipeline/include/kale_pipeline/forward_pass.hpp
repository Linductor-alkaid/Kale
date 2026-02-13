/**
 * @file forward_pass.hpp
 * @brief 简单 Forward Pass：单 Pass 写 Swapchain，GetDrawsForPass(All) 绘制所有提交对象
 *
 * 与 rendering_pipeline_layer_design.md、phase6-6.13 对齐。
 * 提供 ExecuteForwardPass 绘制逻辑与 SetupForwardOnlyRenderGraph 示例。
 */

#pragma once

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_device/command_list.hpp>
#include <kale_scene/scene_types.hpp>

namespace kale::pipeline {

/**
 * 执行简单 Forward Pass 的绘制逻辑：对 GetDrawsForPass(PassFlags::All) 中每项调用
 * renderable->Draw(cmd, worldTransform)。供 AddPass 的 execute 回调使用。
 */
inline void ExecuteForwardPass(const RenderPassContext& ctx, kale_device::CommandList& cmd) {
    auto draws = ctx.GetDrawsForPass(kale::scene::PassFlags::All);
    for (const auto& draw : draws) {
        if (draw.renderable)
            draw.renderable->Draw(cmd, draw.worldTransform, ctx.GetDevice());
    }
}

/**
 * 执行 Forward Pass，使用 RenderPassContext 的 view/projection 计算 MVP 并传入 Draw。
 * 应用层需在 Execute 前调用 RenderGraph::SetViewProjection(view, projection)。
 * 供需要相机矩阵的 Forward 渲染使用。
 */
inline void ExecuteForwardPassWithCamera(const RenderPassContext& ctx, kale_device::CommandList& cmd) {
    auto draws = ctx.GetDrawsForPass(kale::scene::PassFlags::All);
    glm::mat4 view = ctx.GetViewMatrix();
    glm::mat4 proj = ctx.GetProjectionMatrix();
    glm::mat4 vp = proj * view;
    for (const auto& draw : draws) {
        if (draw.renderable) {
            glm::mat4 mvp = vp * draw.worldTransform;
            draw.renderable->Draw(cmd, mvp, ctx.GetDevice());
        }
    }
}

/**
 * 向 RenderGraph 添加单一 Forward Pass（WriteSwapchain，直接绘制到 back buffer）。
 * 应用层在 SetResolution 后调用，然后 Compile(device)。
 * Execute 时该 Pass 会通过 GetDrawsForPass(PassFlags::All) 绘制所有已提交的 Renderable。
 */
inline void SetupForwardOnlyRenderGraph(RenderGraph& rg) {
    rg.AddPass(
        "Forward",
        [](RenderPassBuilder& b) {
            b.WriteSwapchain();
        },
        [](const RenderPassContext& ctx, kale_device::CommandList& cmd) {
            ExecuteForwardPass(ctx, cmd);
        });
}

/**
 * 向 RenderGraph 添加 Forward Pass（使用相机 view/projection 计算 MVP）。
 * 应用层每帧在 Execute 前需调用 rg.SetViewProjection(camera->viewMatrix, camera->projectionMatrix)。
 * 声明深度缓冲以支持 3D 深度测试。
 */
inline void SetupForwardPassWithCamera(RenderGraph& rg) {
    using namespace kale_device;
    TextureDesc depthDesc;
    depthDesc.width = 0;
    depthDesc.height = 0;
    depthDesc.format = Format::D24S8;
    depthDesc.usage = TextureUsage::DepthAttachment | TextureUsage::Sampled;
    RGResourceHandle depthTex = rg.DeclareTexture("ForwardDepth", depthDesc);

    rg.AddPass(
        "Forward",
        [depthTex](RenderPassBuilder& b) {
            b.WriteSwapchain();
            b.WriteDepth(depthTex);
        },
        [](const RenderPassContext& ctx, kale_device::CommandList& cmd) {
            ExecuteForwardPassWithCamera(ctx, cmd);
        });
}

}  // namespace kale::pipeline
