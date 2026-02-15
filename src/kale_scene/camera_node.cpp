/**
 * @file camera_node.cpp
 * @brief CameraNode::UpdateViewProjection 实现
 */

#include <kale_scene/camera_node.hpp>

#include <glm/gtc/matrix_transform.hpp>

namespace kale::scene {

void CameraNode::UpdateViewProjection(float aspectRatio, bool /*flipYForVulkan*/) {
    viewMatrix = glm::inverse(GetWorldMatrix());
    projectionMatrix =
        glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    // Y 轴适配已移至设备层（Vulkan 在 SetViewport 中做 NDC Y 翻转），上层统一使用 Y-up，不再在此翻转
}

}  // namespace kale::scene
