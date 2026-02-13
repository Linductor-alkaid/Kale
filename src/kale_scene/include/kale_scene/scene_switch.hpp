/**
 * @file scene_switch.hpp
 * @brief 场景切换流程：先解绑 SceneNodeRef 再 SetActiveScene，避免悬空引用
 *
 * 与 scene_management_layer_design.md 2.3、phase12-12.9 对齐。
 * SwitchToNewLevel：先 UnbindSceneNodeRefsPointingToSubtree(当前根) 再 SetActiveScene(newRoot)。
 */

#pragma once

#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_node.hpp>

#include <memory>

namespace kale::scene {

class EntityManager;

/**
 * 安全切换活动场景：先解除所有指向当前场景子树的 SceneNodeRef，再激活新场景。
 * 应用层切换关卡时应调用此函数而非直接 SetActiveScene，以避免 ECS 中 SceneNodeRef 悬空。
 * 切换后可为新场景节点重新绑定 SceneNodeRef（如 AddComponent<SceneNodeRef>(e, SceneNodeRef::FromNode(newNode))）。
 *
 * @param sceneMgr 场景管理器
 * @param entityMgr 实体管理器；为 nullptr 时仅执行 SetActiveScene，不解除任何组件
 * @param newRoot 新场景根节点所有权
 */
void SwitchToNewLevel(SceneManager* sceneMgr, EntityManager* entityMgr, std::unique_ptr<SceneNode> newRoot);

}  // namespace kale::scene
