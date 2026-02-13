/**
 * @file scene_node_factories.hpp
 * @brief 场景节点工厂函数：CreateStaticMeshNode、CreateCameraNode
 *
 * phase10-10.4：实现 CreateStaticMeshNode(Mesh* mesh, Material* material)、CreateCameraNode()。
 */

#pragma once

#include <kale_scene/camera_node.hpp>
#include <kale_scene/scene_node.hpp>
#include <kale_scene/static_mesh.hpp>
#include <kale_resource/resource_types.hpp>

#include <memory>

namespace kale::scene {

/**
 * 创建带 StaticMesh Renderable 的场景节点，节点持有 Renderable 所有权。
 * @param mesh 已加载的网格（非占有指针，调用方保证生命周期）
 * @param material 已加载的材质（非占有指针，可为 nullptr）
 * @return 场景节点；可 AddChild 到场景根或其它节点
 */
inline std::unique_ptr<SceneNode> CreateStaticMeshNode(
    kale::resource::Mesh* mesh,
    kale::resource::Material* material) {
    auto node = std::make_unique<SceneNode>();
    node->SetOwnedRenderable(std::make_unique<StaticMesh>(mesh, material));
    return node;
}

/**
 * 创建相机节点。
 * @return 相机节点；可 AddChild 到场景根，应用层需在需要时调用 UpdateViewProjection(aspectRatio)
 */
inline std::unique_ptr<CameraNode> CreateCameraNode() {
    return std::make_unique<CameraNode>();
}

}  // namespace kale::scene
