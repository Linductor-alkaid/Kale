/**
 * @file test_scene_node_ref.cpp
 * @brief phase7-7.1 SceneNodeRef 桥接单元测试
 *
 * 覆盖：IsValid、GetNode(nullptr)、GetNode(manager) 无效/有效句柄、
 * FromNode(nullptr)/FromNode(node)、场景切换后 GetNode 返回 nullptr。
 */

#include <kale_scene/scene_node_ref.hpp>
#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_node.hpp>

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

    // 默认构造：无效
    SceneNodeRef ref0;
    TEST_CHECK(!ref0.IsValid());
    TEST_CHECK(ref0.handle == kInvalidSceneNodeHandle);
    TEST_CHECK(ref0.GetNode(nullptr) == nullptr);

    SceneManager mgr;

    // GetNode(nullptr) 始终返回 nullptr
    ref0.handle = 1;
    TEST_CHECK(ref0.GetNode(nullptr) == nullptr);

    // 空 manager 下任意 handle 解析为 nullptr（未注册）
    TEST_CHECK(ref0.GetNode(&mgr) == nullptr);

    // FromNode(nullptr)
    auto refNull = SceneNodeRef::FromNode(nullptr);
    TEST_CHECK(!refNull.IsValid());
    TEST_CHECK(refNull.GetNode(&mgr) == nullptr);

    // CreateScene 得到根节点，FromNode 后 IsValid 且 GetNode 一致
    auto root = mgr.CreateScene();
    TEST_CHECK(root != nullptr);
    SceneNode* rootPtr = root.get();
    auto refRoot = SceneNodeRef::FromNode(rootPtr);
    TEST_CHECK(refRoot.IsValid());
    TEST_CHECK(refRoot.GetNode(&mgr) == rootPtr);
    TEST_CHECK(refRoot.GetNode(&mgr)->GetHandle() == root->GetHandle());

    // 激活场景后，该根节点仍可解析
    mgr.SetActiveScene(std::move(root));
    TEST_CHECK(refRoot.GetNode(&mgr) == rootPtr);

    // 场景切换：SetActiveScene 新场景后，旧场景被 UnregisterSubtree，旧 handle 解析为 nullptr
    auto newRoot = mgr.CreateScene();
    SceneNodeHandle oldHandle = refRoot.handle;
    mgr.SetActiveScene(std::move(newRoot));  // 销毁旧场景并 Unregister 旧树
    SceneNodeRef refStale;
    refStale.handle = oldHandle;
    TEST_CHECK(refStale.GetNode(&mgr) == nullptr);

    std::cout << "test_scene_node_ref: all checks passed.\n";
    return 0;
}
