/**
 * @file scene_manager.cpp
 * @brief SceneManager 实现：句柄注册表、CreateScene、SetActiveScene、Update、UpdateRecursive、UnregisterSubtree
 */

#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_node.hpp>

namespace kale::scene {

void SceneManager::Update(float deltaTime) {
    (void)deltaTime;
    if (!activeRoot_) return;
    UpdateRecursive(activeRoot_, glm::mat4(1.0f));
}

void SceneManager::UpdateRecursive(SceneNode* node, const glm::mat4& parentWorld) {
    glm::mat4 world = parentWorld * node->GetLocalTransform();
    node->SetWorldMatrix(world);
    for (const auto& child : node->GetChildren())
        UpdateRecursive(child.get(), world);
}

std::unique_ptr<SceneNode> SceneManager::CreateScene() {
    auto root = std::make_unique<SceneNode>();
    RegisterNode(root.get());
    return root;
}

void SceneManager::SetActiveScene(std::unique_ptr<SceneNode> root) {
    if (activeRootStorage_) {
        UnregisterSubtree(activeRootStorage_.get());
        activeRootStorage_.reset();
    }
    activeRoot_ = nullptr;
    if (root) {
        activeRootStorage_ = std::move(root);
        activeRoot_ = activeRootStorage_.get();
    }
}

void SceneManager::UnregisterSubtree(SceneNode* node) {
    if (!node) return;
    for (const auto& child : node->GetChildren())
        UnregisterSubtree(child.get());
    UnregisterNode(node);
}

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
    node->handle_ = h;  // phase5-5.2: SceneNode handle_ 由 SceneManager 在注册时设置
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
