/**
 * @file renderable.hpp
 * @brief 可渲染对象抽象：GetBounds 供 CullScene 视锥剔除
 *
 * 与 scene_management_layer_design.md 5.10 对齐。
 * phase5-5.9：CullScene 需要 node->GetRenderable()->GetBounds()。
 * 完整 GetMesh/GetMaterial/Draw 在 phase5-5.10 实现。
 */

#pragma once

#include <kale_resource/resource_types.hpp>

namespace kale::scene {

/**
 * 可渲染对象抽象基类。
 * 场景剔除时通过 GetBounds() 获取包围盒（局部空间），由 SceneManager 用世界矩阵变换后做视锥测试。
 */
class Renderable {
public:
    virtual ~Renderable() = default;

    /** 返回局部空间包围盒，供 CullScene 用 TransformBounds(bounds, worldMatrix) 得到世界 AABB */
    virtual kale::resource::BoundingBox GetBounds() const = 0;
};

}  // namespace kale::scene
