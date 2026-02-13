/**
 * @file test_render_pipeline_app_integration.cpp
 * @brief phase13-13.20 渲染管线层应用层集成单元测试
 *
 * 覆盖：应用层 OnRender 流程（ClearSubmitted → SubmitVisibleToRenderGraph → Execute）；
 * RenderEngine 初始化时 SetResolution；SetupRenderGraph/SetupForwardOnlyRenderGraph + Compile；
 * Run 主循环中 app->OnRender() 后 device->Present()；完整一帧无崩溃。
 */

#include <kale_engine/render_engine.hpp>
#include <kale_pipeline/forward_pass.hpp>
#include <kale_pipeline/submit_visible.hpp>
#include <kale_pipeline/render_graph.hpp>
#include <kale_scene/scene_manager.hpp>
#include <kale_scene/scene_node_factories.hpp>
#include <kale_scene/camera_node.hpp>
#include <kale_resource/resource_manager.hpp>

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
    kale::RenderEngine engine;
    kale::RenderEngine::Config config;
    config.width = 256;
    config.height = 256;
    config.title = "Kale Pipeline App Integration Test";
    config.enableValidation = false;
    config.useExecutor = false;
    config.assetPath = ".";

    if (!engine.Initialize(config)) {
        std::cerr << "Initialize failed: " << engine.GetLastError() << std::endl;
        std::cout << "test_render_pipeline_app_integration skipped (no display).\n";
        return 0;
    }

    kale::scene::SceneManager* sceneMgr = engine.GetSceneManager();
    kale::pipeline::RenderGraph* rg = engine.GetRenderGraph();
    kale_device::IRenderDevice* device = engine.GetRenderDevice();
    kale::resource::ResourceManager* resourceMgr = engine.GetResourceManager();
    TEST_CHECK(sceneMgr != nullptr);
    TEST_CHECK(rg != nullptr);
    TEST_CHECK(device != nullptr);
    TEST_CHECK(resourceMgr != nullptr);

    // 初始化时 RenderEngine 已对 renderGraph 调用 SetResolution(config.width, config.height)
    rg->SetResolution(config.width, config.height);
    kale::pipeline::SetupForwardOnlyRenderGraph(*rg);
    if (!rg->Compile(device)) {
        std::cerr << "Compile failed: " << rg->GetLastError() << std::endl;
        engine.Shutdown();
        std::exit(1);
    }
    TEST_CHECK(rg->IsCompiled());

    // 最小场景：根 + 一个带占位符的 StaticMesh 节点 + 相机
    std::unique_ptr<kale::scene::SceneNode> root = sceneMgr->CreateScene();
    TEST_CHECK(root != nullptr);
    kale::resource::Mesh* placeholderMesh = resourceMgr->GetPlaceholderMesh();
    kale::resource::Material* placeholderMaterial = resourceMgr->GetPlaceholderMaterial();
    TEST_CHECK(placeholderMesh != nullptr);
    TEST_CHECK(placeholderMaterial != nullptr);
    auto meshNode = kale::scene::CreateStaticMeshNode(placeholderMesh, placeholderMaterial);
    meshNode->SetPassFlags(kale::scene::PassFlags::All);
    root->AddChild(std::move(meshNode));
    auto cameraNode = kale::scene::CreateCameraNode();
    kale::scene::CameraNode* camera = static_cast<kale::scene::CameraNode*>(root->AddChild(std::move(cameraNode)));
    sceneMgr->SetActiveScene(std::move(root));

    struct PipelineIntegrationApp : kale::IApplication {
        kale::RenderEngine* engine = nullptr;
        kale::scene::SceneManager* sceneMgr = nullptr;
        kale::pipeline::RenderGraph* rg = nullptr;
        kale_device::IRenderDevice* device = nullptr;
        kale::scene::CameraNode* camera = nullptr;
        int renderCount = 0;

        void OnUpdate(float) override {}
        void OnRender() override {
            if (!engine || !sceneMgr || !rg || !device || !camera) return;
            rg->ClearSubmitted();
            kale::pipeline::SubmitVisibleToRenderGraph(sceneMgr, rg, camera);
            rg->Execute(device);
            renderCount++;
            if (renderCount >= 1)
                engine->RequestQuit();
        }
    } app;
    app.engine = &engine;
    app.sceneMgr = sceneMgr;
    app.rg = rg;
    app.device = device;
    app.camera = camera;

    engine.Run(&app);

    TEST_CHECK(app.renderCount >= 1);

    engine.Shutdown();
    std::cout << "test_render_pipeline_app_integration passed.\n";
    return 0;
}
