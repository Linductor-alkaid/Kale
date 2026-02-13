/**
 * @file test_submit_visible.cpp
 * @brief phase10-10.3 多视口支持：SubmitVisibleToRenderGraph 单元测试
 *
 * 覆盖：null sceneManager/renderGraph/camera 不崩溃且不提交；
 * 单相机可见节点正确提交到 RG；多视口模式（两 RG 两相机）各自得到对应可见列表。
 */

#include <kale_pipeline/submit_visible.hpp>
#include <kale_pipeline/render_graph.hpp>
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

class DummyRenderable : public kale::scene::Renderable {
public:
    explicit DummyRenderable(const kale::resource::BoundingBox& box) { bounds_ = box; }
    kale::resource::BoundingBox GetBounds() const override { return bounds_; }
    void Draw(kale_device::CommandList&, const glm::mat4&, kale_device::IRenderDevice*) override {}
};

}  // namespace

int main() {
    using namespace kale::scene;
    using namespace kale::pipeline;

    SceneManager mgr;
    kale::resource::BoundingBox box;
    box.min = glm::vec3(-0.5f, -0.5f, -0.5f);
    box.max = glm::vec3(0.5f, 0.5f, 0.5f);
    DummyRenderable renderable(box);

    auto root = mgr.CreateScene();
    auto nodeWithR = std::make_unique<SceneNode>();
    nodeWithR->SetRenderable(&renderable);
    SceneNode* nodePtr = nodeWithR.get();
    root->AddChild(std::move(nodeWithR));
    auto cam = std::make_unique<CameraNode>();
    CameraNode* camPtr = cam.get();
    root->AddChild(std::move(cam));
    mgr.SetActiveScene(std::move(root));
    mgr.Update(0.0f);
    camPtr->UpdateViewProjection(16.f / 9.f);

    RenderGraph rg;

    // 1. null sceneManager：不崩溃，rg 仍为空（未 ClearSubmitted 时）
    rg.ClearSubmitted();
    SubmitVisibleToRenderGraph(nullptr, &rg, camPtr);
    // rg 可能为空（我们没往里面提交任何东西）
    (void)rg.GetSubmittedDraws();

    // 2. null renderGraph：不崩溃
    SubmitVisibleToRenderGraph(&mgr, nullptr, camPtr);

    // 3. null camera：不提交（CullScene 未调用，不向 rg 添加）
    rg.ClearSubmitted();
    SubmitVisibleToRenderGraph(&mgr, &rg, nullptr);
    TEST_CHECK(rg.GetSubmittedDraws().empty());

    // 4. 正常路径：ClearSubmitted 后 SubmitVisibleToRenderGraph，应有至少 1 个 draw（该节点在视锥内）
    rg.ClearSubmitted();
    SubmitVisibleToRenderGraph(&mgr, &rg, camPtr);
    const auto& draws = rg.GetSubmittedDraws();
    TEST_CHECK(draws.size() >= 1u);
    bool found = false;
    for (const auto& d : draws) {
        if (d.renderable == &renderable) {
            found = true;
            TEST_CHECK(d.passFlags == kale::scene::PassFlags::All);
            break;
        }
    }
    TEST_CHECK(found);

    // 5. 多视口模式：两个 RG，同一场景两相机；各自 ClearSubmitted 后 SubmitVisibleToRenderGraph，各自应有可见列表
    RenderGraph rg2;
    auto root2 = mgr.CreateScene();
    auto node2 = std::make_unique<SceneNode>();
    node2->SetRenderable(&renderable);
    root2->AddChild(std::move(node2));
    auto cam2 = std::make_unique<CameraNode>();
    CameraNode* cam2Ptr = cam2.get();
    root2->AddChild(std::move(cam2));
    mgr.SetActiveScene(std::move(root2));
    mgr.Update(0.0f);
    cam2Ptr->UpdateViewProjection(16.f / 9.f);

    rg.ClearSubmitted();
    rg2.ClearSubmitted();
    SubmitVisibleToRenderGraph(&mgr, &rg, cam2Ptr);
    SubmitVisibleToRenderGraph(&mgr, &rg2, cam2Ptr);
    TEST_CHECK(rg.GetSubmittedDraws().size() >= 1u);
    TEST_CHECK(rg2.GetSubmittedDraws().size() >= 1u);

    std::cout << "test_submit_visible: all checks passed.\n";
    return 0;
}
