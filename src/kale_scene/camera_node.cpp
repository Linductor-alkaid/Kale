/**
 * @file camera_node.cpp
 * @brief CameraNode::UpdateViewProjection 实现
 */

#include <kale_scene/camera_node.hpp>

#include <glm/gtc/matrix_transform.hpp>

namespace kale::scene {

void CameraNode::UpdateViewProjection(float aspectRatio) {
    viewMatrix = glm::inverse(GetWorldMatrix());
    projectionMatrix =
        glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
}

}  // namespace kale::scene
