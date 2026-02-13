/**
 * @file test_cull_scene_multi_camera.cpp
 * @brief phase10-10.2 CullScene 多相机单元测试
 *
 * 覆盖：空相机列表返回空 vector、多相机返回等长 vector、
 * visibleByCamera[i] 与 CullScene(cameras[i]) 一致、单相机与多相机重载一致。
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

/** 测试用 Renderable：固定小包围盒，Draw 空实现 */
class DummyRenderable : public kale::scene::Renderable {
public:
    explicit DummyRenderable(const kale::resource::BoundingBox& box) { bounds_ = box; }
    kale::resource::BoundingBox GetBounds() const override { return bounds_; }
    void Draw(kale_device::CommandList&, const glm::mat4&, kale_device::IRenderDevice*) override {}
};

}  // namespace

int main() {
    using namespace kale::scene;

    SceneManager mgr;
    auto root = mgr.CreateScene();
    kale::resource::BoundingBox box;
    box.min = glm::vec3(-0.5f, -0.5f, -0.5f);
    box.max = glm::vec3(0.5f, 0.5f, 0.5f);
    DummyRenderable renderable(box);

    // 根下：一个带 Renderable 的节点（原点附近），两个相机节点
    auto nodeWithRenderable = std::make_unique<SceneNode>();
    nodeWithRenderable->SetRenderable(&renderable);
    SceneNode* nodePtr = nodeWithRenderable.get();
    root->AddChild(std::move(nodeWithRenderable));

    auto cam1 = std::make_unique<CameraNode>();
    CameraNode* cam1Ptr = cam1.get();
    root->AddChild(std::move(cam1));
    auto cam2 = std::make_unique<CameraNode>();
    CameraNode* cam2Ptr = cam2.get();
    root->AddChild(std::move(cam2));

    mgr.SetActiveScene(std::move(root));
    mgr.Update(0.0f);
    cam1Ptr->UpdateViewProjection(16.f / 9.f);
    cam2Ptr->UpdateViewProjection(16.f / 9.f);

    // 单相机剔除：应包含 nodePtr
    std::vector<SceneNode*> single1 = mgr.CullScene(cam1Ptr);
    std::vector<SceneNode*> single2 = mgr.CullScene(cam2Ptr);
    TEST_CHECK(single1.size() >= 1);
    TEST_CHECK(single2.size() >= 1);
    bool found1 = false, found2 = false;
    for (SceneNode* n : single1) if (n == nodePtr) found1 = true;
    for (SceneNode* n : single2) if (n == nodePtr) found2 = true;
    TEST_CHECK(found1);
    TEST_CHECK(found2);

    // 多相机剔除：空列表返回空 vector
    std::vector<CameraNode*> emptyCameras;
    auto emptyResult = mgr.CullScene(emptyCameras);
    TEST_CHECK(emptyResult.empty());

    // 多相机剔除：两相机，返回 size() == 2，且 visibleByCamera[i] 与 CullScene(cameras[i]) 一致
    std::vector<CameraNode*> cameras = { cam1Ptr, cam2Ptr };
    auto visibleByCamera = mgr.CullScene(cameras);
    TEST_CHECK(visibleByCamera.size() == 2u);
    TEST_CHECK(visibleByCamera[0].size() == single1.size());
    TEST_CHECK(visibleByCamera[1].size() == single2.size());
    for (size_t i = 0; i < visibleByCamera[0].size(); ++i)
        TEST_CHECK(visibleByCamera[0][i] == single1[i]);
    for (size_t i = 0; i < visibleByCamera[1].size(); ++i)
        TEST_CHECK(visibleByCamera[1][i] == single2[i]);

    // 单相机列表：等价于单相机重载
    std::vector<CameraNode*> oneCam = { cam1Ptr };
    auto oneResult = mgr.CullScene(oneCam);
    TEST_CHECK(oneResult.size() == 1u);
    TEST_CHECK(oneResult[0].size() == single1.size());
    for (size_t i = 0; i < oneResult[0].size(); ++i)
        TEST_CHECK(oneResult[0][i] == single1[i]);

    std::cout << "test_cull_scene_multi_camera: all checks passed.\n";
    return 0;
}
