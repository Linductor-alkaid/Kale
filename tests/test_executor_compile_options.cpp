/**
 * @file test_executor_compile_options.cpp
 * @brief phase13-13.3 Executor 可选编译选项验证
 *
 * 验证 KALE_EXECUTOR_ENABLE_CHANNELS / KALE_EXECUTOR_ENABLE_TASK_GRAPH 控制
 * 通道与任务图扩展：选项 ON 时对应 API 可用且行为正确，OFF 时可正常编译运行。
 */

#include <kale_executor/render_task_scheduler.hpp>
#if KALE_EXECUTOR_ENABLE_TASK_GRAPH
#include <kale_executor/task_graph.hpp>
#endif

#include <atomic>
#include <cstdlib>
#include <iostream>

#include <executor/executor.hpp>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

int main() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

#if KALE_EXECUTOR_ENABLE_CHANNELS
    auto* ch = sched.GetResourceLoadedChannel();
    TEST_CHECK(ch != nullptr);
    kale::executor::ResourceLoadedEvent ev;
    ev.path = "opt_channel_test";
    ev.resource_handle_id = 1;
    TEST_CHECK(ch->try_send(ev));
    kale::executor::ResourceLoadedEvent recv;
    TEST_CHECK(ch->try_recv(recv));
    TEST_CHECK(recv.path == "opt_channel_test" && recv.resource_handle_id == 1);
#endif

#if KALE_EXECUTOR_ENABLE_TASK_GRAPH
    kale::executor::TaskGraph graph;
    std::atomic<int> ran{0};
    graph.add_task([&ran](const kale::executor::TaskContext&) { ran = 1; });
    sched.SubmitTaskGraph(graph);
    sched.WaitAll();
    graph.wait();
    TEST_CHECK(ran == 1);
#endif

    TEST_CHECK(sched.GetExecutor() == &ex);
    ex.shutdown(true);

    std::cout << "Executor compile options validation passed (CHANNELS="
              << (KALE_EXECUTOR_ENABLE_CHANNELS ? "ON" : "OFF")
              << ", TASK_GRAPH="
              << (KALE_EXECUTOR_ENABLE_TASK_GRAPH ? "ON" : "OFF")
              << ")." << std::endl;
    return 0;
}
