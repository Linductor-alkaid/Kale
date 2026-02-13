/**
 * @file test_load_async.cpp
 * @brief ResourceManager::LoadAsync 与 executor 集成单元测试（phase4-4.3）
 *
 * 覆盖：无 scheduler 同步路径、有 scheduler 异步路径、已缓存返回就绪 Future、
 * 占位符与 SetResource/SetReady、失败路径（无 loader）。
 */

#include <kale_resource/resource_manager.hpp>
#include <kale_executor/render_task_scheduler.hpp>

#include <executor/executor.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

namespace {

struct DummyResource {
    int value = 0;
    explicit DummyResource(int v) : value(v) {}
};

class DummyLoader : public kale::resource::IResourceLoader {
public:
    bool Supports(const std::string& path) const override {
        return path.size() >= 6u && path.find("dummy.") != std::string::npos;
    }

    std::any Load(const std::string& path,
                  kale::resource::ResourceLoadContext& ctx) override {
        (void)path;
        (void)ctx;
        return std::any(static_cast<DummyResource*>(new DummyResource(42)));
    }

    std::type_index GetResourceType() const override {
        return typeid(DummyResource);
    }
};

}  // namespace

static void test_load_async_no_scheduler_sync_path() {
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    rm.RegisterLoader(std::make_unique<DummyLoader>());
    rm.SetAssetPath("");

    kale::executor::ExecutorFuture<kale::resource::ResourceHandle<DummyResource>> fut =
        rm.LoadAsync<DummyResource>("x/dummy.foo");

    TEST_CHECK(fut.valid());
    kale::resource::ResourceHandle<DummyResource> h = fut.get();
    TEST_CHECK(h.IsValid());
    DummyResource* ptr = rm.GetCache().Get(h);
    TEST_CHECK(ptr != nullptr && ptr->value == 42);
    TEST_CHECK(rm.GetCache().IsReady(h));
}

static void test_load_async_with_scheduler_async_path() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    kale::resource::ResourceManager rm(&sched, nullptr, nullptr);
    rm.RegisterLoader(std::make_unique<DummyLoader>());
    rm.SetAssetPath("");

    kale::executor::ExecutorFuture<kale::resource::ResourceHandle<DummyResource>> fut =
        rm.LoadAsync<DummyResource>("a/dummy.bar");

    TEST_CHECK(fut.valid());
    kale::resource::ResourceHandle<DummyResource> h = fut.get();
    TEST_CHECK(h.IsValid());
    DummyResource* ptr = rm.GetCache().Get(h);
    TEST_CHECK(ptr != nullptr && ptr->value == 42);

    sched.WaitAll();
    ex.shutdown(true);
}

static void test_load_async_already_cached_returns_ready_future() {
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    rm.RegisterLoader(std::make_unique<DummyLoader>());
    rm.SetAssetPath("");

    auto fut1 = rm.LoadAsync<DummyResource>("cached/dummy.foo");
    TEST_CHECK(fut1.valid());
    kale::resource::ResourceHandle<DummyResource> h1 = fut1.get();
    TEST_CHECK(h1.IsValid());

    auto fut2 = rm.LoadAsync<DummyResource>("cached/dummy.foo");
    TEST_CHECK(fut2.valid());
    kale::resource::ResourceHandle<DummyResource> h2 = fut2.get();
    TEST_CHECK(h2.IsValid());
    TEST_CHECK(h1.id == h2.id);
}

static void test_load_async_no_loader_returns_invalid_handle() {
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    rm.SetAssetPath("");

    auto fut = rm.LoadAsync<DummyResource>("nonexistent.xyz");
    TEST_CHECK(fut.valid());
    kale::resource::ResourceHandle<DummyResource> h = fut.get();
    TEST_CHECK(!h.IsValid());
    TEST_CHECK(!rm.GetLastError().empty());
}

static void test_load_async_placeholder_then_async_fill() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    kale::resource::ResourceManager rm(&sched, nullptr, nullptr);
    rm.RegisterLoader(std::make_unique<DummyLoader>());
    rm.SetAssetPath("");

    kale::executor::ExecutorFuture<kale::resource::ResourceHandle<DummyResource>> fut =
        rm.LoadAsync<DummyResource>("placeholder/dummy.q");
    TEST_CHECK(fut.valid());
    kale::resource::ResourceHandle<DummyResource> h = fut.get();
    TEST_CHECK(h.IsValid());
    TEST_CHECK(rm.GetCache().IsReady(h));
    TEST_CHECK(rm.GetCache().Get(h) != nullptr);

    sched.WaitAll();
    ex.shutdown(true);
}

int main() {
    test_load_async_no_scheduler_sync_path();
    test_load_async_with_scheduler_async_path();
    test_load_async_already_cached_returns_ready_future();
    test_load_async_no_loader_returns_invalid_handle();
    test_load_async_placeholder_then_async_fill();
    return 0;
}
