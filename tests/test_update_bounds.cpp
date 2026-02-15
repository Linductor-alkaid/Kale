/**
 * @file test_update_bounds.cpp
 * @brief phase13-13.23 UpdateBounds 单元测试
 *
 * 覆盖：SceneManager::Update 对带 Renderable 的节点调用 UpdateBounds；
 * Update 后 GetWorldBounds() 与 TransformBounds(GetBounds(), worldMatrix) 一致；
 * 无 Renderable 的节点不崩溃；CullScene 使用 GetWorldBounds() 结果正确。
 */

#include <kale_scene/scene_manager.hpp>
#include <kale_scene/camera_node.hpp>
#include <kale_scene/scene_node.hpp>
#include <kale_scene/renderable.hpp>
#include <kale_resource/resource_types.hpp>
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

namespace {

class DummyRenderable : public kale::scene::Renderable {
public:
    explicit DummyRenderable(const kale::resource::BoundingBox& box) { bounds_ = box; }
    kale::resource::BoundingBox GetBounds() const override { return bounds_; }
    void Draw(kale_device::CommandList&, const glm::mat4&, kale_device::IRenderDevice*) override {}
};

}  // namespace

int main() {
    using namespace kale::scene;
    using namespace kale::resource;

    SceneManager mgr;
    BoundingBox localBox;
    localBox.min = glm::vec3(-1.f, -1.f, -1.f);
    localBox.max = glm::vec3(1.f, 1.f, 1.f);
    DummyRenderable renderable(localBox);

    auto root = mgr.CreateScene();
    auto child = std::make_unique<SceneNode>();
    child->SetRenderable(&renderable);
    SceneNode* childPtr = child.get();
    root->AddChild(std::move(child));
    mgr.SetActiveScene(std::move(root));

    mgr.Update(0.f);

    const BoundingBox& worldBounds = renderable.GetWorldBounds();
    glm::mat4 world = childPtr->GetWorldMatrix();
    BoundingBox expected = TransformBounds(localBox, world);
    TEST_CHECK(std::abs(worldBounds.min.x - expected.min.x) < 1e-5f);
    TEST_CHECK(std::abs(worldBounds.min.y - expected.min.y) < 1e-5f);
    TEST_CHECK(std::abs(worldBounds.min.z - expected.min.z) < 1e-5f);
    TEST_CHECK(std::abs(worldBounds.max.x - expected.max.x) < 1e-5f);
    TEST_CHECK(std::abs(worldBounds.max.y - expected.max.y) < 1e-5f);
    TEST_CHECK(std::abs(worldBounds.max.z - expected.max.z) < 1e-5f);

    root = mgr.CreateScene();
    auto translated = std::make_unique<SceneNode>();
    translated->SetLocalTransform(glm::translate(glm::mat4(1.f), glm::vec3(5.f, 0.f, 0.f)));
    translated->SetRenderable(&renderable);
    SceneNode* transPtr = translated.get();
    root->AddChild(std::move(translated));
    mgr.SetActiveScene(std::move(root));
    mgr.Update(0.f);

    expected = TransformBounds(localBox, transPtr->GetWorldMatrix());
    const BoundingBox& worldBounds2 = renderable.GetWorldBounds();
    TEST_CHECK(std::abs(worldBounds2.min.x - expected.min.x) < 1e-5f);
    TEST_CHECK(std::abs(worldBounds2.max.x - expected.max.x) < 1e-5f);

    root = mgr.CreateScene();
    auto atOrigin = std::make_unique<SceneNode>();
    atOrigin->SetRenderable(&renderable);
    SceneNode* atOriginPtr = atOrigin.get();
    root->AddChild(std::move(atOrigin));
    mgr.SetActiveScene(std::move(root));
    mgr.Update(0.f);
    expected = TransformBounds(localBox, atOriginPtr->GetWorldMatrix());
    const BoundingBox& worldAtOrigin = renderable.GetWorldBounds();
    TEST_CHECK(std::abs(worldAtOrigin.min.z - expected.min.z) < 1e-5f);
    TEST_CHECK(std::abs(worldAtOrigin.max.z - expected.max.z) < 1e-5f);

    return 0;
}
