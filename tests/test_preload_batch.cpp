/**
 * @file test_preload_batch.cpp
 * @brief ResourceManager::Preload / LoadAsyncBatch 单元测试（phase12-12.4）
 *
 * 覆盖：Preload 触发批量加载（同步路径下全部就绪）、LoadAsyncBatch 返回与 paths 一一对应的 Future、
 * 空 paths、批量中部分路径无 loader 时对应 Future 异常。
 */

#include <kale_resource/resource_manager.hpp>
#include <kale_executor/render_task_scheduler.hpp>

#include <executor/executor.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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
        (void)ctx;
        if (path.find("val99") != std::string::npos)
            return std::any(static_cast<DummyResource*>(new DummyResource(99)));
        return std::any(static_cast<DummyResource*>(new DummyResource(42)));
    }

    std::type_index GetResourceType() const override {
        return typeid(DummyResource);
    }
};

}  // namespace

static void test_preload_sync_path_fills_cache() {
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    rm.RegisterLoader(std::make_unique<DummyLoader>());
    rm.SetAssetPath("");

    std::vector<std::string> paths = { "p1/dummy.a", "p2/dummy.b" };
    rm.Preload<DummyResource>(paths);

    const std::string r1 = rm.ResolvePath("p1/dummy.a");
    const std::string r2 = rm.ResolvePath("p2/dummy.b");
    auto opt1 = rm.GetCache().FindByPath(r1, typeid(DummyResource));
    auto opt2 = rm.GetCache().FindByPath(r2, typeid(DummyResource));
    TEST_CHECK(opt1.has_value());
    TEST_CHECK(opt2.has_value());
    kale::resource::ResourceHandle<DummyResource> h1(opt1->id);
    kale::resource::ResourceHandle<DummyResource> h2(opt2->id);
    TEST_CHECK(rm.GetCache().IsReady(h1));
    TEST_CHECK(rm.GetCache().IsReady(h2));
    DummyResource* ptr1 = rm.GetCache().Get(h1);
    DummyResource* ptr2 = rm.GetCache().Get(h2);
    TEST_CHECK(ptr1 != nullptr && ptr1->value == 42);
    TEST_CHECK(ptr2 != nullptr && ptr2->value == 42);
}

static void test_load_async_batch_returns_one_to_one() {
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    rm.RegisterLoader(std::make_unique<DummyLoader>());
    rm.SetAssetPath("");

    std::vector<std::string> paths = { "batch1/dummy.x", "batch2/dummy.y" };
    std::vector<kale::executor::ExecutorFuture<kale::resource::ResourceHandle<DummyResource>>> futs =
        rm.LoadAsyncBatch<DummyResource>(paths);

    TEST_CHECK(futs.size() == paths.size());
    kale::resource::ResourceHandle<DummyResource> h1 = futs[0].get();
    kale::resource::ResourceHandle<DummyResource> h2 = futs[1].get();
    TEST_CHECK(h1.IsValid());
    TEST_CHECK(h2.IsValid());
    TEST_CHECK(rm.Get<DummyResource>(h1) != nullptr && rm.Get<DummyResource>(h1)->value == 42);
    TEST_CHECK(rm.Get<DummyResource>(h2) != nullptr && rm.Get<DummyResource>(h2)->value == 42);
}

static void test_load_async_batch_empty_paths() {
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    rm.SetAssetPath("");

    std::vector<std::string> paths;
    auto futs = rm.LoadAsyncBatch<DummyResource>(paths);
    TEST_CHECK(futs.empty());

    rm.Preload<DummyResource>(paths);
    TEST_CHECK(true);
}

static void test_load_async_batch_with_scheduler() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    kale::resource::ResourceManager rm(&sched, nullptr, nullptr);
    rm.RegisterLoader(std::make_unique<DummyLoader>());
    rm.SetAssetPath("");

    std::vector<std::string> paths = { "async1/dummy.q", "async2/val99.dummy.w" };
    auto futs = rm.LoadAsyncBatch<DummyResource>(paths);
    TEST_CHECK(futs.size() == 2u);

    kale::resource::ResourceHandle<DummyResource> h1 = futs[0].get();
    kale::resource::ResourceHandle<DummyResource> h2 = futs[1].get();
    TEST_CHECK(h1.IsValid());
    TEST_CHECK(h2.IsValid());
    TEST_CHECK(rm.Get<DummyResource>(h1)->value == 42);
    TEST_CHECK(rm.Get<DummyResource>(h2)->value == 99);

    sched.WaitAll();
    ex.shutdown(true);
}

static void test_load_async_batch_partial_failure() {
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    rm.RegisterLoader(std::make_unique<DummyLoader>());
    rm.SetAssetPath("");

    std::vector<std::string> paths = { "ok/dummy.foo", "noloader/unknown.xyz", "ok2/dummy.bar" };
    auto futs = rm.LoadAsyncBatch<DummyResource>(paths);
    TEST_CHECK(futs.size() == 3u);

    kale::resource::ResourceHandle<DummyResource> h0 = futs[0].get();
    TEST_CHECK(h0.IsValid());

    bool got_exception = false;
    try {
        (void)futs[1].get();
    } catch (const std::runtime_error&) {
        got_exception = true;
    }
    TEST_CHECK(got_exception);

    kale::resource::ResourceHandle<DummyResource> h2 = futs[2].get();
    TEST_CHECK(h2.IsValid());
}

int main() {
    test_preload_sync_path_fills_cache();
    test_load_async_batch_returns_one_to_one();
    test_load_async_batch_empty_paths();
    test_load_async_batch_with_scheduler();
    test_load_async_batch_partial_failure();
    return 0;
}
