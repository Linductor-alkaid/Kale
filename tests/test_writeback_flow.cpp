/**
 * @file test_writeback_flow.cpp
 * @brief phase7-7.6 写回流程示例单元测试
 *
 * 覆盖：PhysicsSystem 读取 PhysicsComponent 通过 SceneNodeRef 写回 SetLocalTransform；
 * AnimationSystem 声明依赖 PhysicsSystem，在物理之后应用 localOffset；
 * 验证 DAG 顺序与最终节点变换正确。
 */

#include <kale_scene/entity_manager.hpp>
#include <kale_scene/entity.hpp>
#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_node_ref.hpp>
#include <kale_scene/writeback_systems.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

static bool mat4_near(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (std::abs(a[i][j] - b[i][j]) > eps) return false;
    return true;
}

int main() {
    using namespace kale::scene;

    // --- 写回结果：Physics 写 T_phys，Animation 写 current * T_anim => 最终 T_phys * T_anim ---
    SceneManager sceneMgr;
    std::unique_ptr<SceneNode> root = sceneMgr.CreateScene();
    TEST_CHECK(root != nullptr);
    sceneMgr.SetActiveScene(std::move(root));
    SceneNode* node = sceneMgr.GetActiveRoot();
    TEST_CHECK(node != nullptr);

    EntityManager em(nullptr, &sceneMgr);
    Entity e = em.CreateEntity();
    glm::mat4 T_phys = glm::translate(glm::mat4(1.0f), glm::vec3(1.f, 0.f, 0.f));
    glm::mat4 T_anim = glm::scale(glm::mat4(1.0f), glm::vec3(2.f, 2.f, 2.f));
    em.AddComponent<PhysicsComponent>(e).localTransform = T_phys;
    em.AddComponent<SceneNodeRef>(e) = SceneNodeRef::FromNode(node);
    em.AddComponent<AnimationComponent>(e).localOffset = T_anim;

    em.RegisterSystem(std::make_unique<PhysicsSystem>());
    em.RegisterSystem(std::make_unique<AnimationSystem>());
    em.Update(0.016f);

    glm::mat4 expected = T_phys * T_anim;
    TEST_CHECK(mat4_near(node->GetLocalTransform(), expected));

    // --- 无 SceneManager 时 Update 不崩溃 ---
    EntityManager emNoScene(nullptr, nullptr);
    Entity e2 = emNoScene.CreateEntity();
    emNoScene.AddComponent<PhysicsComponent>(e2);
    emNoScene.RegisterSystem(std::make_unique<PhysicsSystem>());
    emNoScene.Update(0.016f);

    // --- 无效 SceneNodeRef（无对应节点）时 GetNode 返回 nullptr，不写回，不崩溃 ---
    EntityManager emInvalidRef(nullptr, &sceneMgr);
    Entity e3 = emInvalidRef.CreateEntity();
    emInvalidRef.AddComponent<PhysicsComponent>(e3).localTransform = T_phys;
    SceneNodeRef invalidRef;
    invalidRef.handle = 99999u;  // 不存在的 handle
    emInvalidRef.AddComponent<SceneNodeRef>(e3) = invalidRef;
    emInvalidRef.RegisterSystem(std::make_unique<PhysicsSystem>());
    emInvalidRef.Update(0.016f);
    TEST_CHECK(mat4_near(node->GetLocalTransform(), expected));  // 节点未被错误写入

    std::cout << "test_writeback_flow: all checks passed.\n";
    return 0;
}
