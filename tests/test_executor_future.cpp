// ExecutorPromise / ExecutorFuture 单元测试
// 覆盖生产环境关键场景：异常传播、并发、边界条件

#include <kale_executor/executor_future.hpp>

#include <atomic>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

static void test_promise_future_basic() {
    kale::executor::ExecutorPromise<int> p;
    auto f = p.get_future();
    TEST_CHECK(f.valid());

    p.set_value(42);
    int v = f.get();
    TEST_CHECK(v == 42);
    TEST_CHECK(!f.valid());
}

static void test_promise_future_exception() {
    kale::executor::ExecutorPromise<int> p;
    auto f = p.get_future();
    p.set_exception(std::make_exception_ptr(std::runtime_error("test error")));

    bool threw = false;
    try {
        (void)f.get();
    } catch (const std::runtime_error& e) {
        TEST_CHECK(std::string(e.what()) == "test error");
        threw = true;
    }
    TEST_CHECK(threw);
    TEST_CHECK(!f.valid());
}

static void test_promise_future_void() {
    kale::executor::ExecutorPromise<void> p;
    auto f = p.get_future();
    TEST_CHECK(f.valid());

    p.set_value();
    f.get();
    TEST_CHECK(!f.valid());
}

static void test_valid_after_get() {
    kale::executor::ExecutorPromise<int> p;
    auto f = p.get_future();
    p.set_value(1);
    TEST_CHECK(f.valid());
    (void)f.get();
    TEST_CHECK(!f.valid());
}

static void test_valid_after_then() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    kale::executor::ExecutorPromise<int> p;
    auto f = p.get_future();
    p.set_value(42);

    auto next = f.then(ex, [](int x) { return x + 1; });
    TEST_CHECK(!f.valid());
    TEST_CHECK(next.valid());

    int v = next.get();
    TEST_CHECK(v == 43);
    TEST_CHECK(!next.valid());

    ex.shutdown(true);
}

static void test_then_continuation() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    kale::executor::ExecutorPromise<int> p;
    auto f = p.get_future();

    std::thread t([&p]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        p.set_value(100);
    });

    auto next = f.then(ex, [](int x) { return x * 2; });
    int v = next.get();
    TEST_CHECK(v == 200);

    t.join();
    ex.shutdown(true);
}

static void test_then_void_to_value() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    kale::executor::ExecutorPromise<void> p;
    auto f = p.get_future();
    p.set_value();

    auto next = f.then(ex, []() { return 42; });
    TEST_CHECK(next.valid());
    int v = next.get();
    TEST_CHECK(v == 42);

    ex.shutdown(true);
}

static void test_then_value_to_void() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    kale::executor::ExecutorPromise<int> p;
    auto f = p.get_future();
    p.set_value(42);

    std::atomic<int> side_effect{0};
    auto next = f.then(ex, [&side_effect](int x) {
        side_effect = x;
    });
    next.get();
    TEST_CHECK(side_effect == 42);

    ex.shutdown(true);
}

static void test_then_propagates_exception() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    kale::executor::ExecutorPromise<int> p;
    auto f = p.get_future();
    p.set_exception(std::make_exception_ptr(std::runtime_error("from promise")));

    auto next = f.then(ex, [](int) { return 0; });

    bool threw = false;
    try {
        (void)next.get();
    } catch (const std::runtime_error& e) {
        TEST_CHECK(std::string(e.what()) == "from promise");
        threw = true;
    }
    TEST_CHECK(threw);

    ex.shutdown(true);
}

static void test_async_load() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    auto f = kale::executor::async_load<int>(ex, []() { return 123; });
    TEST_CHECK(f.valid());
    int v = f.get();
    TEST_CHECK(v == 123);
    TEST_CHECK(!f.valid());

    ex.shutdown(true);
}

static void test_async_load_lambda() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    const char* path = "/assets/texture.png";
    auto f = kale::executor::async_load<std::string>(ex, [path]() {
        return std::string("loaded: ") + path;
    });
    std::string v = f.get();
    TEST_CHECK(v == "loaded: /assets/texture.png");

    ex.shutdown(true);
}

static void test_async_load_void() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    std::atomic<bool> done{false};
    auto f = kale::executor::async_load<void>(ex, [&done]() { done = true; });
    f.get();
    TEST_CHECK(done);

    ex.shutdown(true);
}

// --- 生产环境关键补充测试 ---

// 1. continuation 自身抛异常（then 中 func 抛异常应传播到 next.get()）
static void test_then_continuation_throws() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    kale::executor::ExecutorPromise<int> p;
    auto f = p.get_future();
    p.set_value(42);

    auto next = f.then(ex, [](int) -> int {
        throw std::runtime_error("continuation failed");
    });

    bool threw = false;
    try {
        (void)next.get();
    } catch (const std::runtime_error& e) {
        TEST_CHECK(std::string(e.what()) == "continuation failed");
        threw = true;
    }
    TEST_CHECK(threw);

    ex.shutdown(true);
}

// 2. async_load loader 抛异常（资源加载失败典型场景）
static void test_async_load_throws() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    auto f = kale::executor::async_load<int>(ex, []() -> int {
        throw std::runtime_error("load failed: file not found");
    });

    bool threw = false;
    try {
        (void)f.get();
    } catch (const std::runtime_error& e) {
        TEST_CHECK(std::string(e.what()) == "load failed: file not found");
        threw = true;
    }
    TEST_CHECK(threw);

    ex.shutdown(true);
}

// 3. 默认构造 ExecutorFuture valid() 为 false
static void test_default_future_invalid() {
    kale::executor::ExecutorFuture<int> f;
    TEST_CHECK(!f.valid());
}

// 4. 移动后原 future 失效
static void test_move_invalidates() {
    kale::executor::ExecutorPromise<int> p;
    auto f1 = p.get_future();
    p.set_value(1);

    auto f2 = std::move(f1);
    TEST_CHECK(!f1.valid());
    TEST_CHECK(f2.valid());
    TEST_CHECK(f2.get() == 1);
}

// 5. then 链式调用（生产环境常见模式）
static void test_then_chain() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    kale::executor::ExecutorPromise<int> p;
    auto f = p.get_future();
    p.set_value(10);

    auto result = f.then(ex, [](int x) { return x + 1; })
                     .then(ex, [](int x) { return x * 2; })
                     .then(ex, [](int x) { return x - 3; });

    int v = result.get();
    TEST_CHECK(v == 19);  // (10+1)*2-3 = 19

    ex.shutdown(true);
}

// 6. 并发 async_load（模拟多资源同时加载）
static void test_concurrent_async_load() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    constexpr int count = 100;
    std::vector<kale::executor::ExecutorFuture<int>> futures;
    futures.reserve(count);

    for (int i = 0; i < count; ++i) {
        int cap = i;
        futures.push_back(kale::executor::async_load<int>(ex, [cap]() {
            return cap * cap;
        }));
    }

    for (int i = 0; i < count; ++i) {
        int v = futures[i].get();
        TEST_CHECK(v == i * i);
    }

    ex.shutdown(true);
}

// 7. 非拷贝类型 unique_ptr（验证移动语义正确）
static void test_async_load_unique_ptr() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});

    auto f = kale::executor::async_load<std::unique_ptr<int>>(ex, []() {
        return std::make_unique<int>(99);
    });

    auto ptr = f.get();
    TEST_CHECK(ptr != nullptr);
    TEST_CHECK(*ptr == 99);

    ex.shutdown(true);
}

// 8. 多线程 set_value + get（生产者-消费者模式）
static void test_cross_thread_set_and_get() {
    kale::executor::ExecutorPromise<std::string> p;
    auto f = p.get_future();

    std::thread producer([&p]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        p.set_value("from worker");
    });

    std::thread consumer([&f]() {
        std::string v = f.get();
        TEST_CHECK(v == "from worker");
    });

    producer.join();
    consumer.join();
}

int main() {
    // 基础功能
    test_promise_future_basic();
    test_promise_future_exception();
    test_promise_future_void();
    test_valid_after_get();
    test_valid_after_then();
    test_then_continuation();
    test_then_void_to_value();
    test_then_value_to_void();
    test_then_propagates_exception();
    test_async_load();
    test_async_load_lambda();
    test_async_load_void();

    // 生产环境关键补充
    test_then_continuation_throws();
    test_async_load_throws();
    test_default_future_invalid();
    test_move_invalidates();
    test_then_chain();
    test_concurrent_async_load();
    test_async_load_unique_ptr();
    test_cross_thread_set_and_get();

    std::cout << "All ExecutorFuture tests passed." << std::endl;
    return 0;
}
