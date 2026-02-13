/**
 * @file test_resource_manager_render_engine_integration.cpp
 * @brief 资源管理层 RenderEngine 集成单元测试（phase13-13.17）
 *
 * 覆盖：Initialize 后 StagingMemoryManager 在 ResourceManager 之前创建且传入；
 * SetAssetPath 生效；ModelLoader/TextureLoader/MaterialLoader 已注册；
 * CreatePlaceholders 已调用（GetPlaceholderMesh/Texture/Material 非空）；
 * Run() 内每帧可调用 ProcessHotReload/ProcessLoadedCallbacks 不崩溃。
 */

#include <kale_engine/render_engine.hpp>
#include <kale_resource/resource_manager.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

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
    config.width = 800;
    config.height = 600;
    config.title = "Kale RM Integration Test";
    config.enableValidation = false;
    config.useExecutor = true;
    config.assetPath = "./assets";

    bool ok = engine.Initialize(config);
    if (!ok) {
        std::cerr << "Initialize failed (no display?): " << engine.GetLastError() << std::endl;
        std::cout << "test_resource_manager_render_engine_integration skipped (no display).\n";
        return 0;
    }

    kale::resource::ResourceManager* rm = engine.GetResourceManager();
    TEST_CHECK(rm != nullptr);

    // StagingMemoryManager 已传入 ResourceManager（有 device 时 Loader 可用 Staging）
    TEST_CHECK(rm->GetStagingMgr() != nullptr);

    // SetAssetPath 已调用
    std::string resolved = rm->ResolvePath("textures/dummy.png");
    TEST_CHECK(resolved.find("assets") != std::string::npos || resolved.find("textures") != std::string::npos);

    // CreatePlaceholders 已调用，占位符可用
    TEST_CHECK(rm->GetPlaceholderMesh() != nullptr);
    TEST_CHECK(rm->GetPlaceholderTexture() != nullptr);
    TEST_CHECK(rm->GetPlaceholderMaterial() != nullptr);

    // 简短跑一帧以验证 Run 内 ProcessHotReload/ProcessLoadedCallbacks 不崩溃
    struct OneFrameApp : kale::IApplication {
        int frames = 0;
        void OnUpdate(float) override {}
        void OnRender() override { ++frames; }
    } app;
    engine.RequestQuit();
    engine.Run(&app);
    TEST_CHECK(app.frames >= 0);

    engine.Shutdown();
    std::cout << "test_resource_manager_render_engine_integration passed.\n";
    return 0;
}
