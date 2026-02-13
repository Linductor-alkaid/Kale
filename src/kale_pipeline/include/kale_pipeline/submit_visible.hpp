/**
 * @file submit_visible.hpp
 * @brief 多视口支持：将指定相机的可见节点提交到指定 Render Graph
 *
 * 与 scene_management_layer_design.md 4.3、phase10-10.3 多视口支持对齐。
 * 应用层可对每个视口/相机分别调用：ClearSubmitted() 后 SubmitVisibleToRenderGraph(sceneMgr, rg, camera)，
 * 实现「分别 SubmitRenderable 到各自 RenderTarget 或 Pass 链」；多视口时使用多套 RenderGraph 实例，
 * 每套 Pass 链输出到不同 RenderTarget（小地图、分屏等）与 phase12-12.6 对齐。
 */

#pragma once

#include <kale_pipeline/render_graph.hpp>
#include <kale_scene/camera_node.hpp>
#include <kale_scene/scene_manager.hpp>

namespace kale::pipeline {

/**
 * 将指定相机视锥内的可见节点提交到给定 Render Graph。
 * 内部调用 SceneManager::CullScene(camera)，对每个可见且带 Renderable 的节点调用
 * renderGraph->SubmitRenderable(node->GetRenderable(), node->GetWorldMatrix(), node->GetPassFlags())。
 *
 * 使用方式（多视口）：
 * - 每个视口对应一个 RenderGraph 实例（或一套 Pass 链）。
 * - 每帧对每个视口：renderGraph->ClearSubmitted()；再调用本函数传入该视口的相机。
 * - 随后对每个 RenderGraph 调用 Execute(device)（或 phase12-12.6 的 Execute(device, target)）。
 *
 * @param sceneManager 场景管理器；为 nullptr 时不执行
 * @param renderGraph  目标 Render Graph；为 nullptr 时不执行
 * @param camera       相机节点，用于 CullScene；为 nullptr 时相当于提交空列表（不调用 CullScene）
 */
inline void SubmitVisibleToRenderGraph(kale::scene::SceneManager* sceneManager,
                                       RenderGraph* renderGraph,
                                       kale::scene::CameraNode* camera) {
    if (!sceneManager || !renderGraph) return;
    if (!camera) return;

    std::vector<kale::scene::SceneNode*> visible = sceneManager->CullScene(camera);
    for (kale::scene::SceneNode* node : visible) {
        kale::scene::Renderable* r = node ? node->GetRenderable() : nullptr;
        if (r)
            renderGraph->SubmitRenderable(r, node->GetWorldMatrix(), node->GetPassFlags());
    }
}

}  // namespace kale::pipeline
