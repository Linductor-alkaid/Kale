// submit_task_graph API 单元测试：通过 submit_task_graph(ex, graph) 提交并验证执行

#include <kale_executor/task_graph.hpp>

#include <atomic>
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

static void test_submit_task_graph_single() {
    kale::executor::TaskGraph graph;
    std::atomic<int> run{0};
    graph.add_task([&run](const kale::executor::TaskContext&) { run = 1; });
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::submit_task_graph(ex, graph);
    graph.wait();
    TEST_CHECK(run == 1);
    ex.shutdown(true);
}

static void test_submit_task_graph_chain() {
    kale::executor::TaskGraph graph;
    std::atomic<int> step{0};
    kale::executor::TaskHandle h = graph.add_task(
        [&step](const kale::executor::TaskContext&) { step = 1; });
    h = graph.add_task(
        [&step](const kale::executor::TaskContext&) { step = 2; }, {h});
    h = graph.add_task(
        [&step](const kale::executor::TaskContext&) { step = 3; }, {h});
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::submit_task_graph(ex, graph);
    graph.wait();
    TEST_CHECK(step == 3);
    ex.shutdown(true);
}

static void test_submit_task_graph_dag() {
    kale::executor::TaskGraph graph;
    std::atomic<int> a{0}, b{0}, c{0};
    kale::executor::TaskHandle ha = graph.add_task(
        [&a](const kale::executor::TaskContext&) { a = 1; });
    kale::executor::TaskHandle hb = graph.add_task(
        [&b](const kale::executor::TaskContext&) { b = 1; });
    graph.add_task(
        [&a, &b, &c](const kale::executor::TaskContext&) {
            TEST_CHECK(a == 1 && b == 1);
            c = 1;
        },
        {ha, hb});
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::submit_task_graph(ex, graph);
    graph.wait();
    TEST_CHECK(a == 1 && b == 1 && c == 1);
    ex.shutdown(true);
}

static void test_submit_task_graph_empty() {
    kale::executor::TaskGraph graph;
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::submit_task_graph(ex, graph);
    graph.wait();
    ex.shutdown(true);
}

int main() {
    test_submit_task_graph_empty();
    test_submit_task_graph_single();
    test_submit_task_graph_chain();
    test_submit_task_graph_dag();
    std::cout << "All submit_task_graph tests passed." << std::endl;
    return 0;
}
