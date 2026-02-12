/**
 * @file scene_manager.hpp
 * @brief 场景管理器：句柄注册表、GetHandle/GetNode、节点注册/注销
 *
 * 与 scene_management_layer_design.md 5.5 对齐。
 * phase5-5.1：handleRegistry_、GetHandle、GetNode；节点创建时分配 handle 并注册，销毁时从注册表移除。
 */

#pragma once

#include <kale_scene/scene_types.hpp>

#include <unordered_map>

namespace kale::scene {

class SceneNode;

/**
 * 场景管理器：管理场景图节点句柄注册表，提供句柄与节点双向查找。
 * 节点创建时通过 RegisterNode 分配 handle 并注册；销毁时通过 UnregisterNode 从注册表移除。
 */
class SceneManager {
public:
    SceneManager() = default;
    ~SceneManager() = default;

    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

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
    /** handle -> node，用于 GetNode */
    std::unordered_map<SceneNodeHandle, SceneNode*> handleRegistry_;
    /** node -> handle，用于 GetHandle（节点创建时注册，销毁时移除） */
    std::unordered_map<SceneNode*, SceneNodeHandle> nodeToHandle_;
    /** 下一个可分配的句柄值（从 1 递增，0 保留为无效） */
    SceneNodeHandle nextHandle_ = 1;
};

}  // namespace kale::scene
