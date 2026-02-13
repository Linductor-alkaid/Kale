/**
 * @file test_scene_switch.cpp
 * @brief phase12-12.9 场景切换与悬空引用单元测试
 *
 * 覆盖：IsDescendantOf、UnbindSceneNodeRefsPointingToSubtree、
 * SwitchToNewLevel 先解绑再切换、切换后旧 handle 解析为 nullptr。
 */

#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_node.hpp>
#include <kale_scene/scene_node_ref.hpp>
#include <kale_scene/scene_switch.hpp>
#include <kale_scene/entity_manager.hpp>

#include <cstdlib>
#include <iostream>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

int main() {
    using namespace kale::scene;

    // --- IsDescendantOf ---
    SceneManager sm;
    auto root = sm.CreateScene();
    SceneNode* rootPtr = root.get();
    auto child = std::make_unique<SceneNode>();
    SceneNode* childPtr = child.get();
    rootPtr->AddChild(std::move(child));
    sm.RegisterNode(childPtr);
    auto grand = std::make_unique<SceneNode>();
    SceneNode* grandPtr = grand.get();
    childPtr->AddChild(std::move(grand));
    sm.RegisterNode(grandPtr);

    TEST_CHECK(SceneManager::IsDescendantOf(rootPtr, rootPtr));
    TEST_CHECK(SceneManager::IsDescendantOf(rootPtr, childPtr));
    TEST_CHECK(SceneManager::IsDescendantOf(rootPtr, grandPtr));
    TEST_CHECK(SceneManager::IsDescendantOf(childPtr, childPtr));
    TEST_CHECK(SceneManager::IsDescendantOf(childPtr, grandPtr));
    TEST_CHECK(!SceneManager::IsDescendantOf(childPtr, rootPtr));
    TEST_CHECK(!SceneManager::IsDescendantOf(grandPtr, rootPtr));
    TEST_CHECK(!SceneManager::IsDescendantOf(nullptr, rootPtr));
    TEST_CHECK(!SceneManager::IsDescendantOf(rootPtr, nullptr));

    // 第二棵树，与 root 无关
    auto other = std::make_unique<SceneNode>();
    SceneNode* otherPtr = other.get();
    sm.RegisterNode(otherPtr);
    TEST_CHECK(!SceneManager::IsDescendantOf(rootPtr, otherPtr));
    sm.UnregisterNode(otherPtr);

    // --- UnbindSceneNodeRefsPointingToSubtree & SwitchToNewLevel ---
    sm.SetActiveScene(std::move(root));  // 激活 root 树（含 rootPtr, childPtr, grandPtr）
    EntityManager em(nullptr, &sm);

    Entity e1 = em.CreateEntity();
    Entity e2 = em.CreateEntity();
    em.AddComponent<SceneNodeRef>(e1, SceneNodeRef::FromNode(rootPtr));
    em.AddComponent<SceneNodeRef>(e2, SceneNodeRef::FromNode(grandPtr));
    TEST_CHECK(em.EntitiesWith<SceneNodeRef>().size() == 2u);
    TEST_CHECK(em.GetComponent<SceneNodeRef>(e1)->GetNode(&sm) == rootPtr);
    SceneNode* e2Node = em.GetComponent<SceneNodeRef>(e2)->GetNode(&sm);
    TEST_CHECK(e2Node != nullptr && SceneManager::IsDescendantOf(rootPtr, e2Node));

    // 创建新场景并切换：SwitchToNewLevel 应先解绑指向旧树的 ref，再 SetActiveScene
    auto newRoot = sm.CreateScene();
    SceneNode* newRootPtr = newRoot.get();
    SceneNodeHandle oldRootHandle = sm.GetHandle(rootPtr);
    SceneNodeHandle oldGrandHandle = sm.GetHandle(grandPtr);

    SwitchToNewLevel(&sm, &em, std::move(newRoot));

    // 旧场景已销毁，旧 handle 解析为 nullptr
    TEST_CHECK(sm.GetNode(oldRootHandle) == nullptr);
    TEST_CHECK(sm.GetNode(oldGrandHandle) == nullptr);
    // 解绑后两个实体的 SceneNodeRef 组件应被移除
    TEST_CHECK(em.EntitiesWith<SceneNodeRef>().size() == 0u);
    TEST_CHECK(!em.HasComponent<SceneNodeRef>(e1));
    TEST_CHECK(!em.HasComponent<SceneNodeRef>(e2));
    // 新场景为活动场景
    TEST_CHECK(sm.GetActiveRoot() == newRootPtr);

    // 切换后可为新场景节点重新绑定
    em.AddComponent<SceneNodeRef>(e1, SceneNodeRef::FromNode(newRootPtr));
    TEST_CHECK(em.GetComponent<SceneNodeRef>(e1)->GetNode(&sm) == newRootPtr);

    // SwitchToNewLevel(_, nullptr) 仅 SetActiveScene，不解绑
    auto third = sm.CreateScene();
    SceneNode* thirdPtr = third.get();
    sm.SetActiveScene(std::move(third));
    TEST_CHECK(sm.GetActiveRoot() == thirdPtr);

    std::cout << "test_scene_switch: all checks passed.\n";
    return 0;
}
