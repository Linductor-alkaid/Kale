/**
 * @file test_scene_write_validation.cpp
 * @brief phase13-13.22 写回冲突检测（ENABLE_SCENE_WRITE_VALIDATION）单元测试
 *
 * 需以 -DENABLE_SCENE_WRITE_VALIDATION 编译。
 * 覆盖：NotifySceneNodeWritten 登记；有依赖的两系统（Physics + Animation）写同一节点不触发冲突；帧末 CheckSceneWriteConflicts 不崩溃。
 */

#define ENABLE_SCENE_WRITE_VALIDATION 1

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

    // 有依赖的两系统（PhysicsSystem、AnimationSystem）写同一节点 -> 帧末检查应通过，不断言
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
    em.Update(0.016f);  // NotifySceneNodeWritten 被调用，CheckSceneWriteConflicts 不触发断言（有依赖）

    glm::mat4 expected = T_phys * T_anim;
    TEST_CHECK(mat4_near(node->GetLocalTransform(), expected));

    // 多帧 Update 确保帧末检查每帧执行
    em.Update(0.016f);
    em.Update(0.016f);
    TEST_CHECK(mat4_near(node->GetLocalTransform(), expected));

    std::cout << "test_scene_write_validation: all checks passed (ENABLE_SCENE_WRITE_VALIDATION).\n";
    return 0;
}
