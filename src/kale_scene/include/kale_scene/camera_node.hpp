/**
 * @file camera_node.hpp
 * @brief 相机节点：提供 view/projection 矩阵供 CullScene 视锥剔除
 *
 * 与 scene_management_layer_design.md 5.4 对齐。
 * phase5-5.9：CullScene(CameraNode* camera) 需要相机 viewMatrix、projectionMatrix。
 * 完整 fov/near/far/UpdateViewProjection 见 Phase 4。
 */

#pragma once

#include <kale_scene/scene_node.hpp>

#include <glm/glm.hpp>

namespace kale::scene {

/**
 * 相机节点：继承 SceneNode，提供视图与投影矩阵。
 * 应用层或系统在需要时设置 viewMatrix、projectionMatrix（或后续实现 UpdateViewProjection）。
 */
class CameraNode : public SceneNode {
public:
    CameraNode() = default;
    ~CameraNode() = default;

    /** 视图矩阵（世界空间到相机空间） */
    glm::mat4 viewMatrix{1.0f};
    /** 投影矩阵（相机空间到齐次裁剪空间） */
    glm::mat4 projectionMatrix{1.0f};
};

}  // namespace kale::scene
