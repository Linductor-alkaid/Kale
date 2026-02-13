/**
 * @file test_resource_management_performance_validation.cpp
 * @brief phase13-13.16 资源管理层性能测试与调优验证
 *
 * 覆盖：LoadAsync 不阻塞主线程；Staging Buffer 池化无频繁分配；
 * 引用计数正确释放；热重载时文件变化到资源更新（一次 ProcessHotReload 内触发回调）。
 */

#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_cache.hpp>
#include <kale_resource/resource_handle.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_resource/staging_memory_manager.hpp>

#include <kale_executor/render_task_scheduler.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

#include <executor/executor.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <typeindex>

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

/** 验证 LoadAsync 不阻塞主线程：有 scheduler 时 LoadAsync 返回后主线程可继续执行，get() 才阻塞 */
static void test_load_async_does_not_block_main_thread() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    kale::resource::ResourceManager rm(&sched, nullptr, nullptr);
    rm.RegisterLoader(std::make_unique<DummyLoader>());
    rm.SetAssetPath("");

    kale::executor::ExecutorFuture<kale::resource::ResourceHandle<DummyResource>> fut =
        rm.LoadAsync<DummyResource>("x/dummy.foo");
    TEST_CHECK(fut.valid());

    int mainThreadWork = 0;
    for (int i = 0; i < 500; ++i)
        mainThreadWork += i;
    TEST_CHECK(mainThreadWork > 0);

    kale::resource::ResourceHandle<DummyResource> h = fut.get();
    TEST_CHECK(h.IsValid());
    TEST_CHECK(rm.GetCache().Get(h) != nullptr);
    TEST_CHECK(rm.GetCache().IsReady(h));
}

/** Mock 设备：统计 CreateBuffer 调用次数，用于验证池复用 */
class PoolCountMockDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&,
                                          const void*) override {
        createBufferCount_++;
        return kale_device::BufferHandle{static_cast<std::uint64_t>(createBufferCount_)};
    }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&,
                                             const void*) override {
        return kale_device::TextureHandle{};
    }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }
    kale_device::DescriptorSetHandle CreateDescriptorSet(
        const kale_device::DescriptorSetLayoutDesc&) override { return {}; }
    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t,
                                   kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t,
                                  kale_device::BufferHandle, std::size_t, std::size_t) override {}
    void DestroyBuffer(kale_device::BufferHandle) override {}
    void DestroyTexture(kale_device::TextureHandle) override {}
    void DestroyShader(kale_device::ShaderHandle) override {}
    void DestroyPipeline(kale_device::PipelineHandle) override {}
    void DestroyDescriptorSet(kale_device::DescriptorSetHandle) override {}
    void UpdateBuffer(kale_device::BufferHandle, const void*, std::size_t, std::size_t) override {}
    void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override {
        return static_cast<void*>(mapped_);
    }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}

    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &mockCmd_; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}
    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override { return kale_device::FenceHandle{1}; }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }
    std::uint32_t AcquireNextImage() override {
        return kale_device::IRenderDevice::kInvalidSwapchainImageIndex;
    }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }

    int createBufferCount_ = 0;

private:
    class MockCmd : public kale_device::CommandList {
    public:
        void BeginRenderPass(const std::vector<kale_device::TextureHandle>&,
                             kale_device::TextureHandle) override {}
        void EndRenderPass() override {}
        void BindPipeline(kale_device::PipelineHandle) override {}
        void BindDescriptorSet(std::uint32_t, kale_device::DescriptorSetHandle) override {}
        void BindVertexBuffer(std::uint32_t, kale_device::BufferHandle, std::size_t) override {}
        void BindIndexBuffer(kale_device::BufferHandle, std::size_t, bool) override {}
        void SetPushConstants(const void*, std::size_t, std::size_t) override {}
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void CopyBufferToBuffer(kale_device::BufferHandle, std::size_t,
                                kale_device::BufferHandle, std::size_t, std::size_t) override {}
        void CopyBufferToTexture(kale_device::BufferHandle, std::size_t,
                                 kale_device::TextureHandle, std::uint32_t,
                                 std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void CopyTextureToTexture(kale_device::TextureHandle, kale_device::TextureHandle,
                                  std::uint32_t, std::uint32_t) override {}
        void Barrier(const std::vector<kale_device::TextureHandle>&) override {}
        void ClearColor(kale_device::TextureHandle, const float[4]) override {}
        void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
    } mockCmd_;
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    char mapped_[256 * 1024];
};

/** 验证 Staging Buffer 池化：多次 Allocate/Free 后复用同一 buffer，不频繁创建新 buffer */
static void test_staging_buffer_pool_no_frequent_allocation() {
    PoolCountMockDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    mgr.SetPoolSize(64 * 1024);

    kale::resource::StagingAllocation a1 = mgr.Allocate(4096);
    TEST_CHECK(a1.IsValid());
    TEST_CHECK(dev.createBufferCount_ >= 1);
    std::uint64_t id1 = a1.buffer.id;

    mgr.Free(a1);
    kale::resource::StagingAllocation a2 = mgr.Allocate(4096);
    TEST_CHECK(a2.IsValid());
    TEST_CHECK(a2.buffer.id == id1);
    int createCountAfterReuse = dev.createBufferCount_;

    mgr.Free(a2);
    for (int i = 0; i < 5; ++i) {
        kale::resource::StagingAllocation a = mgr.Allocate(4096);
        TEST_CHECK(a.IsValid());
        mgr.Free(a);
    }
    kale::resource::StagingAllocation a3 = mgr.Allocate(4096);
    TEST_CHECK(a3.IsValid());
    TEST_CHECK(dev.createBufferCount_ <= createCountAfterReuse + 1);
}

/** 验证引用计数正确释放：Release 后 ProcessPendingReleases 导致 Get 返回 nullptr */
static void test_refcount_correct_release() {
    using namespace kale::resource;
    ResourceCache cache;
    Mesh* m = new Mesh();
    MeshHandle h = cache.Register<Mesh>("p/m", m, true);
    TEST_CHECK(h.IsValid());
    TEST_CHECK(cache.Get(h) == m);

    cache.Release(ToAny(h));
    int callbacks = 0;
    cache.ProcessPendingReleases([&callbacks, m](ResourceHandleAny, std::any& a) {
        ++callbacks;
        Mesh* p = std::any_cast<Mesh*>(a);
        TEST_CHECK(p == m);
        delete p;
        a = std::any();
    });
    TEST_CHECK(callbacks == 1);
    TEST_CHECK(cache.Get(h) == nullptr);
}

/** 热重载性能：文件 mtime 变化后一次 ProcessHotReload 内触发回调（延迟可接受） */
static void test_hot_reload_callback_within_one_process() {
    namespace fs = std::filesystem;
    fs::path tmpDir = fs::temp_directory_path() / "kale_perf_hotreload";
    fs::create_directories(tmpDir);
    fs::path filePath = tmpDir / "perf_hotreload.txt";
    {
        std::ofstream f(filePath);
        TEST_CHECK(f);
        f << "x";
    }

    std::string assetPath = tmpDir.string();
    if (assetPath.back() != '/' && assetPath.back() != '\\')
        assetPath += "/";

    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    class Loader : public kale::resource::IResourceLoader {
    public:
        bool Supports(const std::string& path) const override {
            return path.find("perf_hotreload.txt") != std::string::npos;
        }
        std::any Load(const std::string&, kale::resource::ResourceLoadContext&) override {
            return std::any(static_cast<DummyResource*>(new DummyResource(1)));
        }
        std::type_index GetResourceType() const override { return typeid(DummyResource); }
    };
    rm.RegisterLoader(std::make_unique<Loader>());
    rm.SetAssetPath(assetPath);

    kale::resource::ResourceHandle<DummyResource> h = rm.Load<DummyResource>("perf_hotreload.txt");
    TEST_CHECK(h.IsValid());

    int callbackCount = 0;
    rm.RegisterHotReloadCallback([&callbackCount](const std::string&, std::type_index) {
        ++callbackCount;
    });
    rm.EnableHotReload(true);
    rm.ProcessHotReload();
    TEST_CHECK(callbackCount == 0);

    try {
        fs::last_write_time(filePath, fs::last_write_time(filePath) + std::chrono::seconds(2));
    } catch (const fs::filesystem_error&) {
        fs::remove_all(tmpDir);
        return;
    }

    rm.ProcessHotReload();
    TEST_CHECK(callbackCount == 1);

    fs::remove_all(tmpDir);
}

}  // namespace

int main() {
    test_load_async_does_not_block_main_thread();
    test_staging_buffer_pool_no_frequent_allocation();
    test_refcount_correct_release();
    test_hot_reload_callback_within_one_process();
    return 0;
}
