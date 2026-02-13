/**
 * @file scene_node_ref.hpp
 * @brief ECS 与 Scene Graph 的桥接结构：SceneNodeRef
 *
 * 与 scene_management_layer_design.md 5.1 对齐。
 * phase7-7.1：SceneNodeRef 桥接。System 中调用 GetNode 后必须校验 if (!node) continue。
 */

#pragma once

#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_types.hpp>

namespace kale::scene {

/**
 * 安全的场景节点引用组件，用于 ECS 到 Scene Graph 的桥接。
 * 存储 SceneNodeHandle，通过 GetNode(SceneManager*) 解析为 SceneNode* 后写回 SetLocalTransform 等。
 * 节点销毁或场景切换后 GetNode 返回 nullptr，调用方必须校验。
 */
struct SceneNodeRef {
    SceneNodeHandle handle = kInvalidSceneNodeHandle;

    /** 是否绑定到有效节点（handle != kInvalidSceneNodeHandle） */
    bool IsValid() const { return handle != kInvalidSceneNodeHandle; }

    /**
     * 根据场景管理器解析为节点指针。
     * @param sceneMgr 非空时调用 sceneMgr->GetNode(handle)
     * @return 对应节点指针，已销毁或无效句柄或 sceneMgr 为空时返回 nullptr
     */
    SceneNode* GetNode(SceneManager* sceneMgr) const {
        return sceneMgr ? sceneMgr->GetNode(handle) : nullptr;
    }

    /**
     * 从节点指针创建引用。
     * @param node 若为 nullptr 则 handle 为 kInvalidSceneNodeHandle
     */
    static SceneNodeRef FromNode(SceneNode* node) {
        SceneNodeRef ref;
        ref.handle = node ? node->GetHandle() : kInvalidSceneNodeHandle;
        return ref;
    }
};

}  // namespace kale::scene
