/**
 * @file scene_switch.cpp
 * @brief SwitchToNewLevel 实现：先解绑再 SetActiveScene
 */

#include <kale_scene/scene_switch.hpp>
#include <kale_scene/entity_manager.hpp>

namespace kale::scene {

void SwitchToNewLevel(SceneManager* sceneMgr, EntityManager* entityMgr, std::unique_ptr<SceneNode> newRoot) {
    if (!sceneMgr) return;
    if (entityMgr && sceneMgr->GetActiveRoot())
        entityMgr->UnbindSceneNodeRefsPointingToSubtree(sceneMgr, sceneMgr->GetActiveRoot());
    sceneMgr->SetActiveScene(std::move(newRoot), nullptr);
}

}  // namespace kale::scene
