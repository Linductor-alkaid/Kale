// RenderTaskScheduler 单元测试：SubmitRenderTask、SubmitSystemUpdate、LoadResourceAsync、WaitAll、ParallelRecordCommands

#include <kale_executor/render_task_scheduler.hpp>

#include <atomic>
#include <cstdlib>
#include <iostream>
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

static void test_submit_render_task_no_deps() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    std::atomic<int> run{0};
    sched.SubmitRenderTask([&run]() { run = 1; });
    sched.WaitAll();
    TEST_CHECK(run == 1);
    ex.shutdown(true);
}

static void test_submit_render_task_with_deps() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    std::atomic<int> first{0}, second{0};
    // 先通过 executor 直接提交第一个任务并取得 future，再提交依赖该 future 的第二个任务
    std::shared_future<void> f = ex.submit([&first]() { first = 1; }).share();
    sched.SubmitRenderTask([&first, &second]() {
        TEST_CHECK(first == 1);
        second = 2;
    }, {f});
    sched.WaitAll();
    TEST_CHECK(first == 1 && second == 2);
    ex.shutdown(true);
}

static void test_submit_render_task_ordering() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    std::atomic<int> order{0};
    sched.SubmitRenderTask([&order]() { order = 1; });
    sched.WaitAll();
    TEST_CHECK(order == 1);
    sched.SubmitRenderTask([&order]() { order = 2; });
    sched.WaitAll();
    TEST_CHECK(order == 2);
    ex.shutdown(true);
}

static void test_system_update() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    struct MySystem : kale::executor::System {
        float lastDt = 0.f;
        void Update(float deltaTime) override { lastDt = deltaTime; }
    };
    MySystem sys;
    sched.SubmitSystemUpdate(&sys, 0.016f);
    sched.WaitAll();
    TEST_CHECK(sys.lastDt == 0.016f);
    ex.shutdown(true);
}

static void test_load_resource_async() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    auto fut = sched.LoadResourceAsync<int>([]() { return 42; });
    TEST_CHECK(fut.valid());
    TEST_CHECK(fut.get() == 42);
    ex.shutdown(true);
}

static void test_wait_all_multiple() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    std::atomic<int> count{0};
    for (int i = 0; i < 5; ++i)
        sched.SubmitRenderTask([&count]() { count++; });
    sched.WaitAll();
    TEST_CHECK(count == 5);
    ex.shutdown(true);
}

static void test_parallel_record_commands_no_deps() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    std::atomic<int> a{0}, b{0};
    sched.ParallelRecordCommands(
        {
            [&a]() { a = 1; },
            [&b]() { b = 2; },
        },
        {{}, {}});  // no dependencies
    TEST_CHECK(a == 1 && b == 2);
    ex.shutdown(true);
}

static void test_parallel_record_commands_with_deps() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    std::atomic<int> first{0}, second{0}, third{0};
    // Pass0: no deps; Pass1: dep on 0; Pass2: dep on 1
    sched.ParallelRecordCommands(
        {
            [&first]() { first = 1; },
            [&first, &second]() { TEST_CHECK(first == 1); second = 2; },
            [&second, &third]() { TEST_CHECK(second == 2); third = 3; },
        },
        {{}, {0}, {1}});
    TEST_CHECK(first == 1 && second == 2 && third == 3);
    ex.shutdown(true);
}

static void test_submit_task_graph() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    kale::executor::TaskGraph graph;
    std::atomic<int> x{0};
    graph.add_task([&x](const kale::executor::TaskContext&) { x = 1; });
    sched.SubmitTaskGraph(graph);
    sched.WaitAll();
    graph.wait();
    TEST_CHECK(x == 1);
    ex.shutdown(true);
}

static void test_get_executor() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);
    TEST_CHECK(sched.GetExecutor() == &ex);
    ex.shutdown(true);
}

static void test_get_resource_loaded_channel() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    kale::executor::TaskChannel<kale::executor::ResourceLoadedEvent, 32>* ch =
        sched.GetResourceLoadedChannel();
    TEST_CHECK(ch != nullptr);
    TEST_CHECK(sched.GetResourceLoadedChannel() == ch);

    kale::executor::ResourceLoadedEvent ev;
    ev.path = "textures/foo.png";
    ev.resource_handle_id = 42;
    TEST_CHECK(ch->try_send(ev));

    kale::executor::ResourceLoadedEvent recv;
    TEST_CHECK(ch->try_recv(recv));
    TEST_CHECK(recv.path == "textures/foo.png" && recv.resource_handle_id == 42);
    ex.shutdown(true);
}

static void test_get_visible_objects_frame_data() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    kale::executor::FrameData<kale::executor::VisibleObjectList>* fd =
        sched.GetVisibleObjectsFrameData();
    TEST_CHECK(fd != nullptr);
    TEST_CHECK(sched.GetVisibleObjectsFrameData() == fd);

    fd->write_buffer().nodes.push_back(reinterpret_cast<void*>(1));
    fd->write_buffer().nodes.push_back(reinterpret_cast<void*>(2));
    fd->end_frame();
    TEST_CHECK(fd->read_buffer().nodes.size() == 2u);
    TEST_CHECK(fd->read_buffer().nodes[0] == reinterpret_cast<void*>(1));
    TEST_CHECK(fd->read_buffer().nodes[1] == reinterpret_cast<void*>(2));
    ex.shutdown(true);
}

int main() {
    test_get_executor();
    test_submit_render_task_no_deps();
    test_submit_render_task_ordering();
    test_submit_render_task_with_deps();
    test_system_update();
    test_load_resource_async();
    test_wait_all_multiple();
    test_parallel_record_commands_no_deps();
    test_parallel_record_commands_with_deps();
    test_submit_task_graph();
    test_get_resource_loaded_channel();
    test_get_visible_objects_frame_data();
    std::cout << "All RenderTaskScheduler tests passed." << std::endl;
    return 0;
}
