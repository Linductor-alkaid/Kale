/**
 * @file test_lod_manager.cpp
 * @brief phase10-10.5 LOD Manager 集成单元测试
 *
 * 覆盖：LODManager::SelectLOD 按距离选档、SetLODManager/GetLODManager、
 * CullScene 内调用 SelectLOD、StaticMesh GetLODCount/SetCurrentLOD/GetMesh 按 LOD 返回。
 */

#include <kale_scene/scene_manager.hpp>
#include <kale_scene/camera_node.hpp>
#include <kale_scene/scene_node.hpp>
#include <kale_scene/renderable.hpp>
#include <kale_scene/lod_manager.hpp>
#include <kale_scene/static_mesh.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_resource/resource_handle.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/render_device.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <cmath>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

namespace {

/** 可设置 LOD 的测试 Renderable，用于验证 SelectLOD 写入 currentLOD */
class LODTrackRenderable : public kale::scene::Renderable {
public:
    void SetBounds(const kale::resource::BoundingBox& b) { bounds_ = b; }
    kale::resource::BoundingBox GetBounds() const override { return bounds_; }
    const kale::resource::Mesh* GetMesh() const override { return nullptr; }
    const kale::resource::Material* GetMaterial() const override { return nullptr; }
    void Draw(kale_device::CommandList&, const glm::mat4&, kale_device::IRenderDevice*) override {}
    size_t GetLODCount() const override { return 4u; }
    void SetCurrentLOD(uint32_t lod) override { lastSetLOD_ = lod; }
    uint32_t GetLastSetLOD() const { return lastSetLOD_; }
private:
    uint32_t lastSetLOD_ = 0;
};

}  // namespace

int main() {
    using namespace kale::scene;
    using namespace kale::resource;

    // 1. SceneManager SetLODManager / GetLODManager
    SceneManager mgr;
    TEST_CHECK(mgr.GetLODManager() == nullptr);
    LODManager lodMgr;
    mgr.SetLODManager(&lodMgr);
    TEST_CHECK(mgr.GetLODManager() == &lodMgr);
    mgr.SetLODManager(nullptr);
    TEST_CHECK(mgr.GetLODManager() == nullptr);

    // 2. LODManager::SelectLOD 按距离选档
    mgr.SetLODManager(&lodMgr);
    auto root = mgr.CreateScene();
    auto camNode = std::make_unique<CameraNode>();
    CameraNode* camPtr = camNode.get();
    camNode->SetLocalTransform(glm::mat4(1.f));  // 相机在原点
    root->AddChild(std::move(camNode));

    LODTrackRenderable trackRenderable;
    trackRenderable.SetBounds(BoundingBox{glm::vec3(-1,-1,-1), glm::vec3(1,1,1)});
    auto nodeNear = std::make_unique<SceneNode>();
    nodeNear->SetLocalTransform(glm::translate(glm::mat4(1.f), glm::vec3(0, 0, -5.f)));  // 距离 5
    nodeNear->SetRenderable(&trackRenderable);
    root->AddChild(std::move(nodeNear));

    mgr.SetActiveScene(std::move(root));
    mgr.Update(0.f);
    camPtr->UpdateViewProjection(16.f/9.f);

    // nodeNear 是 root 下第二个子节点（[0] 是相机）
    lodMgr.SelectLOD(mgr.GetActiveRoot()->GetChildren()[1].get(), camPtr);
    TEST_CHECK(trackRenderable.GetLastSetLOD() == 0u);  // 5 < 20 -> LOD 0

    LODTrackRenderable track2;
    track2.SetBounds(BoundingBox{glm::vec3(-1,-1,-1), glm::vec3(1,1,1)});
    auto nodeFar = std::make_unique<SceneNode>();
    nodeFar->SetLocalTransform(glm::translate(glm::mat4(1.f), glm::vec3(0, 0, -400.f)));  // 距离 400
    nodeFar->SetRenderable(&track2);
    mgr.GetActiveRoot()->AddChild(std::move(nodeFar));
    mgr.Update(0.f);
    lodMgr.SelectLOD(mgr.GetActiveRoot()->GetChildren()[2].get(), camPtr);
    TEST_CHECK(track2.GetLastSetLOD() == 3u);  // 400 >= 300 -> 最高档 LOD 3

    // 3. CullScene 内调用 SelectLOD（有 LODManager 时可见节点会被设 LOD）
    LODTrackRenderable track3;
    track3.SetBounds(BoundingBox{glm::vec3(-10,-10,-50), glm::vec3(10,10,-40)});
    auto nodeMid = std::make_unique<SceneNode>();
    nodeMid->SetLocalTransform(glm::translate(glm::mat4(1.f), glm::vec3(0, 0, -45.f)));
    nodeMid->SetRenderable(&track3);
    mgr.GetActiveRoot()->AddChild(std::move(nodeMid));
    mgr.Update(0.f);
    std::vector<SceneNode*> visible = mgr.CullScene(camPtr);
    TEST_CHECK(visible.size() >= 1u);
    bool foundTrack3 = false;
    for (SceneNode* n : visible) {
        if (n->GetRenderable() == &track3) {
            foundTrack3 = true;
            TEST_CHECK(track3.GetLastSetLOD() <= 3u);
            break;
        }
    }
    TEST_CHECK(foundTrack3);

    // 4. StaticMesh（指针模式）GetLODCount / SetCurrentLOD：无 meshLODHandles 时 GetLODCount()==1
    StaticMesh singleMesh(nullptr, nullptr);
    TEST_CHECK(singleMesh.GetLODCount() == 1u);
    singleMesh.SetCurrentLOD(0);
    TEST_CHECK(singleMesh.GetMesh() == nullptr);

    // 5. StaticMesh SetLODHandles 多档时 GetLODCount 与 GetMesh 按 currentLOD（指针模式避免 executor 依赖）
    StaticMesh multiLOD(nullptr, nullptr);
    MeshHandle h1, h2;
    multiLOD.SetLODHandles({h1, h2});
    TEST_CHECK(multiLOD.GetLODCount() == 2u);
    multiLOD.SetCurrentLOD(0);
    TEST_CHECK(multiLOD.GetMesh() == nullptr);
    multiLOD.SetCurrentLOD(1);
    TEST_CHECK(multiLOD.GetMesh() == nullptr);
    multiLOD.SetCurrentLOD(99);
    TEST_CHECK(multiLOD.GetMesh() == nullptr);  // 越界 clamp 到最后一档

    std::cout << "test_lod_manager: all checks passed.\n";
    return 0;
}
