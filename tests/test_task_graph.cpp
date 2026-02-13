// TaskGraph 单元测试：DAG 提交、拓扑序、wait、TaskContext、add_task_with_data

#include <kale_executor/task_graph.hpp>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

static void test_empty_graph_submit_wait() {
    kale::executor::TaskGraph graph;
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    graph.submit(ex);
    graph.wait();
    ex.shutdown(true);
}

static void test_single_task() {
    kale::executor::TaskGraph graph;
    std::atomic<int> run{0};
    graph.add_task([&run](const kale::executor::TaskContext&) { run++; });
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    graph.submit(ex);
    graph.wait();
    TEST_CHECK(run == 1);
    ex.shutdown(true);
}

static void test_two_sequential() {
    kale::executor::TaskGraph graph;
    std::atomic<int> first{0}, second{0};
    kale::executor::TaskHandle a = graph.add_task(
        [&first](const kale::executor::TaskContext&) { first = 1; });
    graph.add_task([&first, &second](const kale::executor::TaskContext&) {
        TEST_CHECK(first == 1);
        second = 2;
    }, {a});
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    graph.submit(ex);
    graph.wait();
    TEST_CHECK(first == 1 && second == 2);
    ex.shutdown(true);
}

static void test_dag_three_nodes() {
    // A -> C, B -> C (C depends on A and B)
    kale::executor::TaskGraph graph;
    std::atomic<int> a_done{0}, b_done{0}, c_done{0};
    kale::executor::TaskHandle ha = graph.add_task(
        [&a_done](const kale::executor::TaskContext&) { a_done = 1; });
    kale::executor::TaskHandle hb = graph.add_task(
        [&b_done](const kale::executor::TaskContext&) { b_done = 1; });
    graph.add_task(
        [&a_done, &b_done, &c_done](const kale::executor::TaskContext&) {
            TEST_CHECK(a_done == 1 && b_done == 1);
            c_done = 1;
        },
        {ha, hb});
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    graph.submit(ex);
    graph.wait();
    TEST_CHECK(a_done == 1 && b_done == 1 && c_done == 1);
    ex.shutdown(true);
}

static void test_task_context_handle() {
    kale::executor::TaskGraph graph;
    kale::executor::TaskHandle seen = kale::executor::kInvalidTaskHandle;
    kale::executor::TaskHandle h = graph.add_task(
        [&seen](const kale::executor::TaskContext& ctx) {
            seen = ctx.task_handle;
        });
    TEST_CHECK(h == 1);
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    graph.submit(ex);
    graph.wait();
    TEST_CHECK(seen == 1);
    ex.shutdown(true);
}

static void test_add_task_with_data() {
    // add_task_with_data(f3, {}, {h1, h2}): task 3 has no deps; tasks 1 and 2 depend on 3. Order: 3, then 1 and 2 (any order).
    kale::executor::TaskGraph graph;
    std::vector<int> order;
    std::mutex order_mutex;
    kale::executor::TaskHandle h1 = graph.add_task(
        [&order, &order_mutex](const kale::executor::TaskContext&) {
            std::lock_guard<std::mutex> lock(order_mutex);
            order.push_back(1);
        });
    kale::executor::TaskHandle h2 = graph.add_task(
        [&order, &order_mutex](const kale::executor::TaskContext&) {
            std::lock_guard<std::mutex> lock(order_mutex);
            order.push_back(2);
        });
    graph.add_task_with_data(
        [&order, &order_mutex](const kale::executor::TaskContext&) {
            std::lock_guard<std::mutex> lock(order_mutex);
            order.push_back(3);
        },
        {}, {h1, h2});
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    graph.submit(ex);
    graph.wait();
    TEST_CHECK(order.size() == 3);
    TEST_CHECK(order[0] == 3);
    TEST_CHECK((order[1] == 1 && order[2] == 2) || (order[1] == 2 && order[2] == 1));
    ex.shutdown(true);
}

static void test_clear() {
    kale::executor::TaskGraph graph;
    graph.add_task([](const kale::executor::TaskContext&) {});
    graph.clear();
    TEST_CHECK(!graph.is_submitted());
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    graph.submit(ex);
    graph.wait();
    ex.shutdown(true);
}

static void test_chain_five() {
    kale::executor::TaskGraph graph;
    std::atomic<int> count{0};
    kale::executor::TaskHandle h = graph.add_task(
        [&count](const kale::executor::TaskContext&) { count++; });
    for (int i = 0; i < 4; ++i) {
        h = graph.add_task(
            [&count](const kale::executor::TaskContext&) { count++; },
            {h});
    }
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    graph.submit(ex);
    graph.wait();
    TEST_CHECK(count == 5);
    ex.shutdown(true);
}

int main() {
    test_empty_graph_submit_wait();
    test_single_task();
    test_two_sequential();
    test_dag_three_nodes();
    test_task_context_handle();
    test_add_task_with_data();
    test_clear();
    test_chain_five();
    std::cout << "All TaskGraph tests passed." << std::endl;
    return 0;
}
