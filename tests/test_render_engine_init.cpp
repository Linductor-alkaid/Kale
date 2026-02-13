/**
 * @file test_render_engine_init.cpp
 * @brief RenderEngine 初始化顺序单元测试（phase11-11.8）
 *
 * 覆盖：未初始化时 getters 为 nullptr、GetLastError 为空；
 * Initialize 成功时各 getter 非空、Shutdown 后恢复为空；
 * Initialize 失败时 GetLastError 非空；Shutdown 无崩溃、可重复调用。
 */

#include <kale_engine/render_engine.hpp>

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

    // 未初始化时 getters 为 nullptr，GetLastError 为空
    TEST_CHECK(engine.GetRenderDevice() == nullptr);
    TEST_CHECK(engine.GetInputManager() == nullptr);
    TEST_CHECK(engine.GetWindowSystem() == nullptr);
    TEST_CHECK(engine.GetResourceManager() == nullptr);
    TEST_CHECK(engine.GetEntityManager() == nullptr);
    TEST_CHECK(engine.GetSceneManager() == nullptr);
    TEST_CHECK(engine.GetRenderGraph() == nullptr);
    TEST_CHECK(engine.GetScheduler() == nullptr);
    TEST_CHECK(engine.GetLastError().empty());

    // Shutdown 未初始化不崩溃
    engine.Shutdown();
    engine.Shutdown();

    // Initialize：可能因无显示/Vulkan 失败，两种情况都验证
    kale::RenderEngine::Config config;
    config.width = 800;
    config.height = 600;
    config.title = "Kale Test";
    config.enableValidation = false;

    bool ok = engine.Initialize(config);
    if (ok) {
        TEST_CHECK(engine.GetRenderDevice() != nullptr);
        TEST_CHECK(engine.GetInputManager() != nullptr);
        TEST_CHECK(engine.GetWindowSystem() != nullptr);
        TEST_CHECK(engine.GetResourceManager() != nullptr);
        TEST_CHECK(engine.GetEntityManager() != nullptr);
        TEST_CHECK(engine.GetSceneManager() != nullptr);
        TEST_CHECK(engine.GetRenderGraph() != nullptr);
        TEST_CHECK(engine.GetScheduler() != nullptr);

        engine.Shutdown();
        TEST_CHECK(engine.GetRenderDevice() == nullptr);
        TEST_CHECK(engine.GetInputManager() == nullptr);
        TEST_CHECK(engine.GetWindowSystem() == nullptr);
        TEST_CHECK(engine.GetResourceManager() == nullptr);
        TEST_CHECK(engine.GetEntityManager() == nullptr);
        TEST_CHECK(engine.GetSceneManager() == nullptr);
        TEST_CHECK(engine.GetRenderGraph() == nullptr);
        TEST_CHECK(engine.GetScheduler() == nullptr);
    } else {
        TEST_CHECK(!engine.GetLastError().empty());
    }

    // 再次 Initialize 不崩溃（Shutdown 后或失败后）
    engine.Initialize(config);
    engine.Shutdown();

    std::cout << "test_render_engine_init passed.\n";
    return 0;
}
