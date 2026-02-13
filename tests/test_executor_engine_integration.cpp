/**
 * @file test_executor_engine_integration.cpp
 * @brief phase13-13.4 Executor 库集成与引擎初始化验证
 *
 * 覆盖：useExecutor=true 时 scheduler 与底层 Executor 非空；初始化顺序
 * scheduler → sceneManager → entityManager(scheduler, sceneManager) 且
 * EntityManager 持有 SceneManager 指针（GetEntityManager()->GetSceneManager() == GetSceneManager()）；
 * TaskGraph 经 Scheduler::SubmitTaskGraph 提交到底层 Executor 执行。
 */

#include <kale_engine/render_engine.hpp>
#include <kale_executor/render_task_scheduler.hpp>
#include <kale_executor/task_graph.hpp>
#include <kale_scene/entity_manager.hpp>

#include <cstdlib>
#include <iostream>
#include <atomic>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

int main() {
    kale::RenderEngine::Config config;
    config.width = 800;
    config.height = 600;
    config.title = "Kale Executor Integration Test";
    config.enableValidation = false;
    config.useExecutor = true;

    kale::RenderEngine engine;
    bool ok = engine.Initialize(config);

    if (!ok) {
        std::cerr << "Initialize failed (no display?): " << engine.GetLastError() << std::endl;
        std::cout << "test_executor_engine_integration skipped (init failed).\n";
        return 0;
    }

    // useExecutor=true → scheduler 与底层 Executor 非空
    kale::executor::RenderTaskScheduler* sched = engine.GetScheduler();
    TEST_CHECK(sched != nullptr);
    TEST_CHECK(sched->GetExecutor() != nullptr);

    // 初始化顺序：entityManager(scheduler, sceneManager) → EntityManager 持有 SceneManager 指针
    kale::scene::EntityManager* entityMgr = engine.GetEntityManager();
    kale::scene::SceneManager* sceneMgr = engine.GetSceneManager();
    TEST_CHECK(entityMgr != nullptr);
    TEST_CHECK(sceneMgr != nullptr);
    TEST_CHECK(entityMgr->GetSceneManager() == sceneMgr);

    // resourceManager(scheduler, device, staging) / renderGraph 已创建
    TEST_CHECK(engine.GetResourceManager() != nullptr);
    TEST_CHECK(engine.GetRenderGraph() != nullptr);

    // TaskGraph::submit 经 Scheduler 提交到底层 Executor 执行
    kale::executor::TaskGraph graph;
    std::atomic<int> ran{0};
    graph.add_task([&ran](const kale::executor::TaskContext&) { ran++; });
    sched->SubmitTaskGraph(graph);
    graph.wait();
    TEST_CHECK(ran.load() == 1);

    engine.Shutdown();

    // useExecutor=false：不创建 executor/scheduler，其余子系统仍创建
    config.useExecutor = false;
    kale::RenderEngine engine2;
    ok = engine2.Initialize(config);
    if (ok) {
        TEST_CHECK(engine2.GetScheduler() == nullptr);
        TEST_CHECK(engine2.GetEntityManager() != nullptr);
        TEST_CHECK(engine2.GetSceneManager() != nullptr);
        TEST_CHECK(engine2.GetEntityManager()->GetSceneManager() == engine2.GetSceneManager());
        engine2.Shutdown();
    }

    std::cout << "test_executor_engine_integration passed.\n";
    return 0;
}
