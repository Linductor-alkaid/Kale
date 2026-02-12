/**
 * @file scene_manager.cpp
 * @brief SceneManager 实现：句柄注册表、GetHandle/GetNode、RegisterNode/UnregisterNode
 */

#include <kale_scene/scene_manager.hpp>

namespace kale::scene {

SceneNodeHandle SceneManager::GetHandle(SceneNode* node) const {
    if (!node) return kInvalidSceneNodeHandle;
    auto it = nodeToHandle_.find(node);
    if (it == nodeToHandle_.end()) return kInvalidSceneNodeHandle;
    return it->second;
}

SceneNode* SceneManager::GetNode(SceneNodeHandle handle) const {
    if (handle == kInvalidSceneNodeHandle) return nullptr;
    auto it = handleRegistry_.find(handle);
    if (it == handleRegistry_.end()) return nullptr;
    return it->second;
}

SceneNodeHandle SceneManager::RegisterNode(SceneNode* node) {
    if (!node) return kInvalidSceneNodeHandle;
    SceneNodeHandle h = nextHandle_++;
    handleRegistry_[h] = node;
    nodeToHandle_[node] = h;
    return h;
}

void SceneManager::UnregisterNode(SceneNode* node) {
    if (!node) return;
    auto it = nodeToHandle_.find(node);
    if (it == nodeToHandle_.end()) return;
    SceneNodeHandle h = it->second;
    nodeToHandle_.erase(it);
    handleRegistry_.erase(h);
}

}  // namespace kale::scene
