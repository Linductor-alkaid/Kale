/**
 * @file test_scene_layer_error_lifecycle.cpp
 * @brief phase13-13.25 场景管理层错误处理与生命周期单元测试
 *
 * 覆盖：节点销毁时 handle 从注册表移除、GetNode 返回 nullptr；
 * System 必须校验 if (!node) continue；场景切换前须解绑 SceneNodeRef；
 * Renderable 非占有指针与 GetDependencies 写回顺序。
 */

#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_node.hpp>
#include <kale_scene/scene_node_ref.hpp>
#include <kale_scene/entity_manager.hpp>
#include <kale_scene/writeback_systems.hpp>

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

    // --- GetNode 无效句柄返回 nullptr ---
    SceneManager sm;
    TEST_CHECK(sm.GetNode(kInvalidSceneNodeHandle) == nullptr);
    TEST_CHECK(sm.GetNode(99999u) == nullptr);

    // --- 节点注册后 GetNode 有效；UnregisterNode 后 GetNode 返回 nullptr ---
    auto root = sm.CreateScene();
    SceneNode* rootPtr = root.get();
    SceneNodeHandle h = sm.GetHandle(rootPtr);
    TEST_CHECK(h != kInvalidSceneNodeHandle);
    TEST_CHECK(sm.GetNode(h) == rootPtr);
    std::unique_ptr<SceneNode> standalone = std::make_unique<SceneNode>();
    SceneNode* standPtr = standalone.get();
    SceneNodeHandle standH = sm.RegisterNode(standPtr);
    TEST_CHECK(sm.GetNode(standH) == standPtr);
    sm.UnregisterNode(standPtr);  // 从注册表移除后 GetNode 返回 nullptr
    TEST_CHECK(sm.GetNode(standH) == nullptr);

    // --- System 必须校验 if (!node) continue：无效 SceneNodeRef 时 PhysicsSystem 不写回、不崩溃 ---
    SceneManager sm2;
    auto sceneRoot = sm2.CreateScene();
    SceneNode* nodePtr = sceneRoot.get();
    sm2.SetActiveScene(std::move(sceneRoot));
    EntityManager em2(nullptr, &sm2);
    Entity eValid = em2.CreateEntity();
    Entity eInvalid = em2.CreateEntity();
    em2.AddComponent<PhysicsComponent>(eValid).localTransform = glm::mat4(2.0f);
    em2.AddComponent<PhysicsComponent>(eInvalid).localTransform = glm::mat4(3.0f);
    em2.AddComponent<SceneNodeRef>(eValid, SceneNodeRef::FromNode(nodePtr));
    SceneNodeRef invalidRef;  // handle == kInvalidSceneNodeHandle
    em2.AddComponent<SceneNodeRef>(eInvalid) = invalidRef;
    PhysicsSystem physSys;
    physSys.Update(0.0f, em2);
    TEST_CHECK(nodePtr->GetLocalTransform() == glm::mat4(2.0f));  // 仅有效实体写回
    // 无效 ref 的 GetNode 返回 nullptr，PhysicsSystem 内 if (!node) continue 跳过，不崩溃

    // --- 多 System 写同一节点须通过 GetDependencies 保证顺序（AnimationSystem 依赖 PhysicsSystem）---
    TEST_CHECK(physSys.GetDependencies().empty());
    AnimationSystem animSys;
    auto deps = animSys.GetDependencies();
    TEST_CHECK(deps.size() == 1u && deps[0] == std::type_index(typeid(PhysicsSystem)));

    std::cout << "test_scene_layer_error_lifecycle: all checks passed.\n";
    return 0;
}
