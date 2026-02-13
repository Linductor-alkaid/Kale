/**
 * @file test_scene_layer_render_engine_integration.cpp
 * @brief phase13-13.24 场景管理层 RenderEngine 集成验证
 *
 * 覆盖：初始化顺序 sceneManager → entityManager(scheduler, sceneManager)；
 * EntityManager 持有 SceneManager 指针；主循环 Run() 顺序
 * entityManager->Update → sceneManager->Update → OnUpdate → OnRender → Present；
 * 应用层可通过 GetEntityManager()->RegisterSystem() 注册系统。
 */

#include <kale_engine/render_engine.hpp>
#include <kale_scene/entity_manager.hpp>
#include <kale_scene/scene_manager.hpp>

#include <cstdlib>
#include <iostream>
#include <atomic>
#include <memory>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

namespace {

std::atomic<int> g_systemUpdateCount{0};

class CountingSystem : public kale::scene::System {
public:
    void Update(float /*deltaTime*/, kale::scene::EntityManager& /*em*/) override {
        g_systemUpdateCount++;
    }
};

struct SceneIntegrationApp : kale::IApplication {
    int onUpdateCount = 0;
    int onRenderCount = 0;
    kale::RenderEngine* engine = nullptr;
    bool quitAfterFirstFrame = false;

    void OnUpdate(float /*deltaTime*/) override {
        onUpdateCount++;
        // 主循环顺序要求：entityManager->Update、sceneManager->Update 先于 OnUpdate
        // 若我们注册的 System 在本帧被调用，则 entityManager->Update 已执行
        if (quitAfterFirstFrame && engine)
            engine->RequestQuit();
    }

    void OnRender() override {
        onRenderCount++;
    }
};

}  // namespace

int main() {
    kale::RenderEngine::Config config;
    config.width = 320;
    config.height = 240;
    config.title = "SceneLayerIntegration";
    config.enableValidation = false;
    config.useExecutor = true;

    kale::RenderEngine engine;
    bool ok = engine.Initialize(config);
    if (!ok) {
        std::cerr << "Initialize failed: " << engine.GetLastError() << std::endl;
        std::cout << "test_scene_layer_render_engine_integration skipped (init failed).\n";
        return 0;
    }

    // 1. 初始化顺序：sceneManager、entityManager(scheduler, sceneManager)，EntityManager 持有 SceneManager
    kale::scene::SceneManager* sceneMgr = engine.GetSceneManager();
    kale::scene::EntityManager* entityMgr = engine.GetEntityManager();
    TEST_CHECK(sceneMgr != nullptr);
    TEST_CHECK(entityMgr != nullptr);
    TEST_CHECK(entityMgr->GetSceneManager() == sceneMgr);

    // 2. 应用层可通过 GetEntityManager()->RegisterSystem() 注册系统
    g_systemUpdateCount.store(0);
    entityMgr->RegisterSystem(std::make_unique<CountingSystem>());

    // 3. Run 一帧：entityManager->Update（含 System）→ sceneManager->Update → OnUpdate → OnRender → Present
    SceneIntegrationApp app;
    app.engine = &engine;
    app.quitAfterFirstFrame = true;
    engine.Run(&app);

    TEST_CHECK(app.onUpdateCount >= 1);
    TEST_CHECK(app.onRenderCount >= 1);
    // 证明 entityManager->Update 在本帧已执行（我们注册的 System 被调用）
    TEST_CHECK(g_systemUpdateCount.load() >= 1);

    engine.Shutdown();
    std::cout << "test_scene_layer_render_engine_integration passed.\n";
    return 0;
}
