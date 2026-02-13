/**
 * @file lod_manager.cpp
 * @brief LODManager::SelectLOD 实现：按相机与节点距离选择 LOD
 */

#include <kale_scene/lod_manager.hpp>
#include <kale_scene/scene_node.hpp>
#include <kale_scene/camera_node.hpp>
#include <kale_scene/renderable.hpp>

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace kale::scene {

void LODManager::SelectLOD(SceneNode* node, CameraNode* camera) const {
    if (!node || !camera) return;

    Renderable* r = node->GetRenderable();
    if (!r) return;

    const size_t lodCount = r->GetLODCount();
    if (lodCount <= 1u) {
        r->SetCurrentLOD(0);
        return;
    }

    glm::vec3 camPos(glm::inverse(camera->viewMatrix)[3]);
    glm::vec3 nodePos(node->GetWorldMatrix()[3]);
    float dist = glm::length(camPos - nodePos);

    uint32_t level = 0u;
    for (float t : distanceThresholds_) {
        if (dist < t) break;
        ++level;
    }
    level = static_cast<uint32_t>(std::min(static_cast<size_t>(level), lodCount - 1));
    r->SetCurrentLOD(level);
}

}  // namespace kale::scene
