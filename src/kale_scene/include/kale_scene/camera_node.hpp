/**
 * @file camera_node.hpp
 * @brief 相机节点：提供 view/projection 矩阵供 CullScene 视锥剔除
 *
 * 与 scene_management_layer_design.md 5.4 对齐。
 * phase10-10.1：fov、nearPlane、farPlane、UpdateViewProjection()。
 */

#pragma once

#include <kale_scene/scene_node.hpp>

#include <glm/glm.hpp>

namespace kale::scene {

/**
 * 相机节点：继承 SceneNode，提供视图与投影矩阵。
 * 应用层或系统在需要时调用 UpdateViewProjection(aspectRatio) 根据节点世界矩阵与 fov/near/far 更新矩阵。
 */
class CameraNode : public SceneNode {
public:
    CameraNode() = default;
    ~CameraNode() = default;

    /** 垂直视场角（度），默认 60 */
    float fov = 60.0f;
    /** 近裁剪面，默认 0.1 */
    float nearPlane = 0.1f;
    /** 远裁剪面，默认 1000 */
    float farPlane = 1000.0f;

    /** 视图矩阵（世界空间到相机空间） */
    glm::mat4 viewMatrix{1.0f};
    /** 投影矩阵（相机空间到齐次裁剪空间） */
    glm::mat4 projectionMatrix{1.0f};

    /**
     * 根据当前节点世界矩阵与 fov/nearPlane/farPlane 更新 viewMatrix 与 projectionMatrix。
     * 由应用层或系统在需要时调用（如每帧或相机/视口变化后）。
     * @param aspectRatio 视口宽高比，默认 16/9
     */
    void UpdateViewProjection(float aspectRatio = 16.f / 9.f);
};

}  // namespace kale::scene
