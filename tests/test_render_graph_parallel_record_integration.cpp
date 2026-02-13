// phase9-9.7 Render Graph 并行录制集成：验证 executor 层与 Render Graph 并行录制所用模式一致
// 即：按 DAG 分组提交 SubmitRenderTask、依赖 prevLevelFutures、每任务写自己的 result 槽、最后按序收集

#include <kale_executor/render_task_scheduler.hpp>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include <executor/executor.hpp>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

// 模拟 RenderGraph::RecordPasses 的集成模式：拓扑分组 → 每层内并行 SubmitRenderTask，依赖上一层 futures → 结果写入 result[topoPos]
static void test_dag_submit_and_collect_results() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    // 模拟 3 个 “Pass”：level0 = [0], level1 = [1, 2]，即 0 先执行，1 和 2 依赖 0 后并行
    std::vector<int> result(3, -1);
    std::vector<std::shared_future<void>> prevLevelFutures;

    // Level 0: 单任务
    prevLevelFutures.push_back(sched.SubmitRenderTask(
        [&result]() { result[0] = 0; },
        {}));
    for (auto& f : prevLevelFutures)
        if (f.valid()) f.wait();

    // Level 1: 两任务并行，均依赖 level0
    std::vector<std::shared_future<void>> level1;
    level1.push_back(sched.SubmitRenderTask(
        [&result]() { result[1] = 1; },
        prevLevelFutures));
    level1.push_back(sched.SubmitRenderTask(
        [&result]() { result[2] = 2; },
        prevLevelFutures));
    for (auto& f : level1)
        if (f.valid()) f.wait();

    sched.WaitAll();

    TEST_CHECK(result[0] == 0);
    TEST_CHECK(result[1] == 1);
    TEST_CHECK(result[2] == 2);
    ex.shutdown(true);
}

// 多层级 DAG：0 → 1,2 → 3（1 和 2 并行，3 依赖 1 和 2）
static void test_three_level_dag() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    std::vector<int> result(4, -1);
    std::vector<std::shared_future<void>> prevLevelFutures;

    prevLevelFutures.push_back(sched.SubmitRenderTask(
        [&result]() { result[0] = 0; },
        {}));
    for (auto& f : prevLevelFutures) if (f.valid()) f.wait();

    std::vector<std::shared_future<void>> level1;
    level1.push_back(sched.SubmitRenderTask(
        [&result]() { result[1] = 1; },
        prevLevelFutures));
    level1.push_back(sched.SubmitRenderTask(
        [&result]() { result[2] = 2; },
        prevLevelFutures));
    for (auto& f : level1) if (f.valid()) f.wait();

    prevLevelFutures = std::move(level1);
    sched.SubmitRenderTask(
        [&result]() { result[3] = 3; },
        prevLevelFutures);
    sched.WaitAll();

    TEST_CHECK(result[0] == 0 && result[1] == 1 && result[2] == 2 && result[3] == 3);
    ex.shutdown(true);
}

// 单层多任务（同组内并行，模拟 GetTopologicalGroups 同组）
static void test_same_level_parallel() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    std::atomic<int> count{0};
    std::vector<std::shared_future<void>> deps;
    std::vector<std::shared_future<void>> group;
    for (int i = 0; i < 4; ++i)
        group.push_back(sched.SubmitRenderTask([&count]() { count++; }, deps));
    for (auto& f : group)
        if (f.valid()) f.wait();
    sched.WaitAll();
    TEST_CHECK(count == 4);
    ex.shutdown(true);
}

int main() {
    test_dag_submit_and_collect_results();
    test_three_level_dag();
    test_same_level_parallel();
    return 0;
}
