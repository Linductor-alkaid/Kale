/**
 * @file renderable.hpp
 * @brief 可渲染对象抽象：GetBounds、GetMesh/GetMaterial、Draw
 *
 * 与 scene_management_layer_design.md、phase5-5.10 对齐。
 * CullScene 使用 GetBounds()；渲染管线使用 GetMesh/GetMaterial 与 Draw。
 */

#pragma once

#include <kale_device/command_list.hpp>
#include <kale_resource/resource_types.hpp>
#include <glm/glm.hpp>

namespace kale::scene {

/**
 * 可渲染对象抽象基类。
 * - 场景剔除：GetBounds() 返回局部空间包围盒，由 SceneManager 用世界矩阵变换后做视锥测试。
 * - 渲染：GetMesh()/GetMaterial() 提供资源，Draw() 向 CommandList 录制绘制命令。
 */
class Renderable {
public:
    virtual ~Renderable() = default;

    /** 返回局部空间包围盒，供 CullScene 用 TransformBounds(bounds, worldMatrix) 得到世界 AABB */
    virtual kale::resource::BoundingBox GetBounds() const = 0;

    /** 返回网格资源，无网格时返回 nullptr（如粒子、全屏四边形等） */
    virtual const kale::resource::Mesh* GetMesh() const { return nullptr; }

    /** 返回材质资源，无材质时返回 nullptr */
    virtual const kale::resource::Material* GetMaterial() const { return nullptr; }

    /** 向命令列表录制绘制命令，worldTransform 为节点世界矩阵 */
    virtual void Draw(kale_device::CommandList& cmd, const glm::mat4& worldTransform) = 0;

protected:
    /** 局部空间包围盒，子类可设置后由 GetBounds() 返回，供 CullScene 使用 */
    kale::resource::BoundingBox bounds_{};
};

}  // namespace kale::scene
