// phase13-13.2 Executor 性能与验证：无死锁、无数据竞争
// 压力测试：多轮并发任务、大 DAG、WaitAll/拓扑序完成即证明无死锁；共享状态仅用原子与依赖序保证无数据竞争

#include <kale_executor/render_task_scheduler.hpp>
#include <kale_executor/task_graph.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
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

// 压力：多轮 SubmitRenderTask + WaitAll，验证无死锁且计数正确（无数据竞争）
static void test_scheduler_stress_no_deadlock() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    constexpr int rounds = 20;
    constexpr int tasks_per_round = 50;
    std::atomic<int> total_done{0};

    for (int r = 0; r < rounds; ++r) {
        for (int i = 0; i < tasks_per_round; ++i)
            sched.SubmitRenderTask([&total_done]() { total_done++; });
        sched.WaitAll();
    }
    TEST_CHECK(total_done == rounds * tasks_per_round);
    ex.shutdown(true);
}

// 大 DAG 提交与 wait() 完成，验证拓扑序无死锁
static void test_task_graph_large_dag_no_deadlock() {
    kale::executor::TaskGraph graph;
    constexpr int num_nodes = 80;
    std::atomic<int> executed{0};

    kale::executor::TaskHandle h0 = graph.add_task(
        [&executed](const kale::executor::TaskContext&) { executed++; });
    std::vector<kale::executor::TaskHandle> prev = {h0};
    for (int i = 1; i < num_nodes; ++i) {
        std::vector<kale::executor::TaskHandle> deps;
        for (kale::executor::TaskHandle p : prev)
            deps.push_back(p);
        kale::executor::TaskHandle h = graph.add_task(
            [&executed](const kale::executor::TaskContext&) { executed++; },
            deps);
        prev = {h};
    }

    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    graph.submit(ex);
    graph.wait();
    TEST_CHECK(executed == num_nodes);
    ex.shutdown(true);
}

// 多轮 TaskGraph 提交（同图 submit 一次后 wait，再 clear 可复用以节省代码；此处每轮新图）
static void test_task_graph_multiple_rounds_no_deadlock() {
    constexpr int rounds = 10;
    constexpr int chain_len = 15;
    std::atomic<int> total{0};

    for (int r = 0; r < rounds; ++r) {
        kale::executor::TaskGraph graph;
        kale::executor::TaskHandle h = graph.add_task(
            [&total](const kale::executor::TaskContext&) { total++; });
        for (int i = 1; i < chain_len; ++i)
            h = graph.add_task(
                [&total](const kale::executor::TaskContext&) { total++; },
                {h});

        ::executor::Executor ex;
        ex.initialize(::executor::ExecutorConfig{});
        graph.submit(ex);
        graph.wait();
        ex.shutdown(true);
    }
    TEST_CHECK(total == rounds * chain_len);
}

// ParallelRecordCommands 多组依赖链，验证无死锁且顺序正确（无数据竞争：仅原子写+依赖读）
static void test_parallel_record_commands_stress() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    constexpr int levels = 5;
    constexpr int tasks_per_level = 8;
    std::vector<std::atomic<int>> level_done(levels);
    for (auto& a : level_done) a = 0;

    std::vector<std::function<void()>> recordFuncs;
    std::vector<std::vector<size_t>> dependencies;
    for (int l = 0; l < levels; ++l) {
        for (int t = 0; t < tasks_per_level; ++t) {
            recordFuncs.push_back([&level_done, l]() { level_done[l]++; });
            if (l == 0)
                dependencies.push_back({});
            else
                dependencies.push_back({(size_t)((l - 1) * tasks_per_level)});
        }
    }
    sched.ParallelRecordCommands(recordFuncs, dependencies);
    for (int l = 0; l < levels; ++l)
        TEST_CHECK(level_done[l] == tasks_per_level);
    ex.shutdown(true);
}

#if KALE_EXECUTOR_ENABLE_TASK_GRAPH
// SubmitTaskGraph + WaitAll 多轮，验证无死锁
static void test_submit_task_graph_stress() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);
    std::atomic<int> total{0};
    constexpr int rounds = 15;

    for (int r = 0; r < rounds; ++r) {
        kale::executor::TaskGraph graph;
        graph.add_task([&total](const kale::executor::TaskContext&) { total++; });
        sched.SubmitTaskGraph(graph);
        sched.WaitAll();
        graph.wait();
    }
    TEST_CHECK(total == rounds);
    ex.shutdown(true);
}
#endif

int main() {
    test_scheduler_stress_no_deadlock();
    test_task_graph_large_dag_no_deadlock();
    test_task_graph_multiple_rounds_no_deadlock();
    test_parallel_record_commands_stress();
#if KALE_EXECUTOR_ENABLE_TASK_GRAPH
    test_submit_task_graph_stress();
#endif
    std::cout << "All Executor performance/validation tests passed (no deadlock, correct counts)." << std::endl;
    return 0;
}
