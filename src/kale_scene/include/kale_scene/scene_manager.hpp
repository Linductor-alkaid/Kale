/**
 * @file scene_manager.hpp
 * @brief 场景管理器：句柄注册表、场景生命周期、GetHandle/GetNode
 *
 * 与 scene_management_layer_design.md 5.5 对齐。
 * phase5-5.1：handleRegistry_、GetHandle、GetNode。
 * phase5-5.3：CreateScene、SetActiveScene、GetActiveRoot；销毁旧场景时递归 Unregister 并从 handleRegistry 移除。
 */

#pragma once

#include <kale_scene/scene_node.hpp>
#include <kale_scene/scene_types.hpp>

#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>

namespace kale::scene {

/**
 * 场景管理器：管理场景图节点句柄注册表与活动场景生命周期。
 * 节点创建时通过 RegisterNode 分配 handle 并注册；销毁时通过 UnregisterNode 从注册表移除。
 * SetActiveScene 销毁旧场景（递归 Unregister 后释放），激活新场景并取得所有权。
 */
class SceneManager {
public:
    SceneManager() = default;
    ~SceneManager() = default;

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    /**
     * 创建根节点，分配 handle 并注册。
     * @return 根节点所有权，调用方可继续 AddChild 后通过 SetActiveScene 激活
     */
    std::unique_ptr<SceneNode> CreateScene();

    /**
     * 销毁旧场景（递归从 handleRegistry 移除并释放），激活新场景并取得所有权。
     * @param root 新场景根节点所有权；若为 nullptr 则仅销毁当前活动场景
     */
    void SetActiveScene(std::unique_ptr<SceneNode> root);

    /**
     * 每帧更新：递归计算活动场景中所有节点的世界矩阵。
     * 应在 ECS 写回 Scene Graph 之后、OnRender 之前调用。
     * @param deltaTime 帧间隔（保留供后续 UpdateBounds 等扩展）
     */
    void Update(float deltaTime);

    /**
     * 返回当前活动场景根节点；无活动场景时返回 nullptr。
     */
    SceneNode* GetActiveRoot() const { return activeRoot_; }

    /**
     * 根据节点指针查找其句柄。
     * @param node 非空且已注册的节点
     * @return 对应句柄，未注册则返回 kInvalidSceneNodeHandle
     */
    SceneNodeHandle GetHandle(SceneNode* node) const;

    /**
     * 根据句柄解析节点指针。
     * @param handle 由 RegisterNode 分配的句柄
     * @return 对应节点指针，已销毁或无效句柄则返回 nullptr
     */
    SceneNode* GetNode(SceneNodeHandle handle) const;

    /**
     * 为节点分配新句柄并注册到注册表。
     * 节点创建时（CreateScene/AddChild）调用。
     * @param node 非空节点指针
     * @return 分配得到的 SceneNodeHandle（永不为 kInvalidSceneNodeHandle）
     */
    SceneNodeHandle RegisterNode(SceneNode* node);

    /**
     * 将节点从注册表移除。
     * 节点销毁时调用，之后 GetNode(handle) 返回 nullptr。
     * @param node 已注册的节点指针；若未注册则无操作
     */
    void UnregisterNode(SceneNode* node);

private:
    /** 递归计算世界矩阵：world = parentWorld * node->GetLocalTransform()，并递归子节点 */
    void UpdateRecursive(SceneNode* node, const glm::mat4& parentWorld);

    /** 递归将子树所有节点从注册表移除（先子后父） */
    void UnregisterSubtree(SceneNode* node);

    /** handle -> node，用于 GetNode */
    std::unordered_map<SceneNodeHandle, SceneNode*> handleRegistry_;
    /** node -> handle，用于 GetHandle（节点创建时注册，销毁时移除） */
    std::unordered_map<SceneNode*, SceneNodeHandle> nodeToHandle_;
    /** 下一个可分配的句柄值（从 1 递增，0 保留为无效） */
    SceneNodeHandle nextHandle_ = 1;
    /** 当前活动场景根节点所有权；释放时递归销毁整棵树 */
    std::unique_ptr<SceneNode> activeRootStorage_;
    /** 当前活动场景根节点指针，与 activeRootStorage_ 一致 */
    SceneNode* activeRoot_ = nullptr;
};

}  // namespace kale::scene
