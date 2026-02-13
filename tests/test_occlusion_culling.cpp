/**
 * @file test_occlusion_culling.cpp
 * @brief phase12-12.8 遮挡剔除单元测试
 *
 * 覆盖：SetEnableOcclusionCulling/IsOcclusionCullingEnabled、SetOcclusionHiZBuffer/GetOcclusionHiZBuffer、
 * 禁用时 CullScene 与无遮挡一致、启用且无 Hi-Z 时软件遮挡剔除（后方节点被剔除）、
 * 启用且设置 Hi-Z 时列表不变（预留路径 no-op）。
 */

#include <kale_scene/scene_manager.hpp>
#include <kale_scene/camera_node.hpp>
#include <kale_scene/scene_node.hpp>
#include <kale_scene/renderable.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/render_device.hpp>

#include <glm/glm.hpp>
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

/** 测试用 Renderable：固定包围盒 */
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

    // 1. 开关与 Hi-Z 接口
    TEST_CHECK(!mgr.IsOcclusionCullingEnabled());
    mgr.SetEnableOcclusionCulling(true);
    TEST_CHECK(mgr.IsOcclusionCullingEnabled());
    mgr.SetEnableOcclusionCulling(false);
    TEST_CHECK(!mgr.IsOcclusionCullingEnabled());

    TEST_CHECK(mgr.GetOcclusionHiZBuffer() == nullptr);
    void* fakeHiZ = reinterpret_cast<void*>(1u);
    mgr.SetOcclusionHiZBuffer(fakeHiZ);
    TEST_CHECK(mgr.GetOcclusionHiZBuffer() == fakeHiZ);
    mgr.SetOcclusionHiZBuffer(nullptr);
    TEST_CHECK(mgr.GetOcclusionHiZBuffer() == nullptr);

    // 2. 场景：相机在原点看 -Z，前物体 Z -3..-2，后物体 Z -1.5..-1（视空间前=更负 Z）
    auto root = mgr.CreateScene();
    auto camNode = std::make_unique<CameraNode>();
    camNode->SetLocalTransform(glm::mat4(1.f));
    CameraNode* camPtr = camNode.get();
    root->AddChild(std::move(camNode));
    camPtr->UpdateViewProjection(16.f / 9.f);

    BoundingBox frontBox;
    frontBox.min = glm::vec3(-0.5f, -0.5f, -3.f);
    frontBox.max = glm::vec3(0.5f, 0.5f, -2.f);
    BoundingBox backBox;
    backBox.min = glm::vec3(-0.5f, -0.5f, -1.5f);
    backBox.max = glm::vec3(0.5f, 0.5f, -1.f);
    DummyRenderable frontRenderable(frontBox);
    DummyRenderable backRenderable(backBox);

    auto frontNode = std::make_unique<SceneNode>();
    frontNode->SetLocalTransform(glm::mat4(1.f));
    frontNode->SetRenderable(&frontRenderable);
    auto backNode = std::make_unique<SceneNode>();
    backNode->SetLocalTransform(glm::mat4(1.f));
    backNode->SetRenderable(&backRenderable);
    root->AddChild(std::move(frontNode));
    root->AddChild(std::move(backNode));
    mgr.SetActiveScene(std::move(root));
    mgr.Update(0.f);

    // 3. 禁用遮挡剔除：两节点均可见
    mgr.SetEnableOcclusionCulling(false);
    mgr.SetOcclusionHiZBuffer(nullptr);
    std::vector<SceneNode*> visibleNoOcclusion = mgr.CullScene(camPtr);
    TEST_CHECK(visibleNoOcclusion.size() == 2u);

    // 4. 启用遮挡剔除、无 Hi-Z：前物体遮挡后物体，应只剩 1 个
    mgr.SetEnableOcclusionCulling(true);
    mgr.SetOcclusionHiZBuffer(nullptr);
    std::vector<SceneNode*> visibleWithOcclusion = mgr.CullScene(camPtr);
    TEST_CHECK(visibleWithOcclusion.size() == 1u);

    // 5. 启用遮挡剔除但设置 Hi-Z：预留路径 no-op，列表不变（两节点都保留）
    mgr.SetOcclusionHiZBuffer(fakeHiZ);
    std::vector<SceneNode*> visibleWithHiZ = mgr.CullScene(camPtr);
    TEST_CHECK(visibleWithHiZ.size() == 2u);
    mgr.SetOcclusionHiZBuffer(nullptr);

    // 6. 空场景或单节点：不崩溃，单节点不被剔除
    auto root2 = mgr.CreateScene();
    auto cam2 = std::make_unique<CameraNode>();
    cam2->SetLocalTransform(glm::mat4(1.f));
    CameraNode* cam2Ptr = cam2.get();
    root2->AddChild(std::move(cam2));
    cam2Ptr->UpdateViewProjection(16.f / 9.f);
    auto singleNode = std::make_unique<SceneNode>();
    singleNode->SetLocalTransform(glm::mat4(1.f));
    BoundingBox singleBox;
    singleBox.min = glm::vec3(-1.f, -1.f, -2.f);
    singleBox.max = glm::vec3(1.f, 1.f, -1.f);
    DummyRenderable singleRenderable(singleBox);
    singleNode->SetRenderable(&singleRenderable);
    root2->AddChild(std::move(singleNode));
    mgr.SetActiveScene(std::move(root2));
    mgr.Update(0.f);
    mgr.SetEnableOcclusionCulling(true);
    std::vector<SceneNode*> singleVisible = mgr.CullScene(cam2Ptr);
    TEST_CHECK(singleVisible.size() == 1u);

    return 0;
}
