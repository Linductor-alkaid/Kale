/**
 * @file test_cull_scene_single_camera.cpp
 * @brief phase5-5.9 CullScene 单相机单元测试
 *
 * 覆盖：null 相机返回空、无活动场景返回空、无 Renderable 节点不加入列表、
 * 视锥内节点可见、视锥外节点不可见、层级递归（仅带 Renderable 的节点入列表）。
 */

#include <kale_scene/scene_manager.hpp>
#include <kale_scene/camera_node.hpp>
#include <kale_scene/scene_node.hpp>
#include <kale_scene/renderable.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/render_device.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <iostream>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

namespace {

/** 测试用 Renderable：固定包围盒，Draw 空实现 */
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
    BoundingBox inViewBox;
    inViewBox.min = glm::vec3(-0.5f, -0.5f, -3.f);
    inViewBox.max = glm::vec3(0.5f, 0.5f, -2.f);
    BoundingBox behindBox;
    behindBox.min = glm::vec3(-1.f, -1.f, 1.f);
    behindBox.max = glm::vec3(1.f, 1.f, 2.f);
    DummyRenderable inViewRenderable(inViewBox);
    DummyRenderable behindRenderable(behindBox);

    // 1. CullScene(nullptr) 返回空
    std::vector<SceneNode*> emptyNull = mgr.CullScene(nullptr);
    TEST_CHECK(emptyNull.empty());

    // 2. 无活动场景时 CullScene(camera) 返回空（未 SetActiveScene）
    auto camOnly = std::make_unique<CameraNode>();
    CameraNode* camPtr = camOnly.get();
    camOnly->UpdateViewProjection(16.f / 9.f);
    std::vector<SceneNode*> emptyNoScene = mgr.CullScene(camPtr);
    TEST_CHECK(emptyNoScene.empty());

    // 3. 有场景、有相机，但场景中只有无 Renderable 的节点 -> 可见列表为空
    auto root = mgr.CreateScene();
    auto childOnly = std::make_unique<SceneNode>();
    root->AddChild(std::move(childOnly));
    auto camNode = std::make_unique<CameraNode>();
    CameraNode* camNodePtr = camNode.get();
    root->AddChild(std::move(camNode));
    mgr.SetActiveScene(std::move(root));
    mgr.Update(0.0f);
    camNodePtr->UpdateViewProjection(16.f / 9.f);
    std::vector<SceneNode*> noRenderable = mgr.CullScene(camNodePtr);
    TEST_CHECK(noRenderable.empty());

    // 4. 单节点带 Renderable 且在视锥内 -> 可见列表含该节点
    root = mgr.CreateScene();
    auto withRenderable = std::make_unique<SceneNode>();
    withRenderable->SetRenderable(&inViewRenderable);
    SceneNode* withRenderablePtr = withRenderable.get();
    root->AddChild(std::move(withRenderable));
    camNode = std::make_unique<CameraNode>();
    camNodePtr = camNode.get();
    root->AddChild(std::move(camNode));
    mgr.SetActiveScene(std::move(root));
    mgr.Update(0.0f);
    camNodePtr->UpdateViewProjection(16.f / 9.f);
    std::vector<SceneNode*> visible = mgr.CullScene(camNodePtr);
    TEST_CHECK(visible.size() == 1u);
    TEST_CHECK(visible[0] == withRenderablePtr);

    // 5. 节点在相机后方（世界空间）-> 视锥剔除，可见列表为空
    root = mgr.CreateScene();
    auto behindNode = std::make_unique<SceneNode>();
    behindNode->SetLocalTransform(glm::translate(glm::mat4(1.f), glm::vec3(0.f, 0.f, 5.f)));
    behindNode->SetRenderable(&behindRenderable);
    root->AddChild(std::move(behindNode));
    camNode = std::make_unique<CameraNode>();
    camNodePtr = camNode.get();
    root->AddChild(std::move(camNode));
    mgr.SetActiveScene(std::move(root));
    mgr.Update(0.0f);
    camNodePtr->UpdateViewProjection(16.f / 9.f);
    std::vector<SceneNode*> culled = mgr.CullScene(camNodePtr);
    TEST_CHECK(culled.empty());

    // 6. 层级：根无 Renderable，子节点有 Renderable 且在视锥内 -> 仅子节点在可见列表
    root = mgr.CreateScene();
    auto parent = std::make_unique<SceneNode>();
    auto kid = std::make_unique<SceneNode>();
    kid->SetRenderable(&inViewRenderable);
    SceneNode* kidPtr = kid.get();
    parent->AddChild(std::move(kid));
    root->AddChild(std::move(parent));
    camNode = std::make_unique<CameraNode>();
    camNodePtr = camNode.get();
    root->AddChild(std::move(camNode));
    mgr.SetActiveScene(std::move(root));
    mgr.Update(0.0f);
    camNodePtr->UpdateViewProjection(16.f / 9.f);
    std::vector<SceneNode*> hierarchy = mgr.CullScene(camNodePtr);
    TEST_CHECK(hierarchy.size() == 1u);
    TEST_CHECK(hierarchy[0] == kidPtr);

    std::cout << "test_cull_scene_single_camera: all checks passed.\n";
    return 0;
}
