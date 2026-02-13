/**
 * @file test_staging_memory_manager.cpp
 * @brief phase6-6.1 / phase6-6.3 StagingMemoryManager 与 Upload Queue 单元测试
 *
 * 覆盖：null/零 size Allocate 返回无效；有效 device Allocate 返回有效且可写；
 * Free 即时回收、Free(alloc,fence) 延迟回收、ReclaimCompleted 回收已 signal 的；
 * SubmitUpload(cmd=null) 加入 pending、FlushUploads 执行并返回 Fence；
 * phase6-6.3：SubmitUpload(cmd 非空) 立即录制不加入 pending、Buffer→Texture pending 经 FlushUploads 触发 CopyBufferToTexture。
 */

#include <kale_resource/staging_memory_manager.hpp>

#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                               \
    } while (0)

namespace {

/** Mock CommandList 供 FlushUploads 录制 Copy 命令 */
class MockCommandList : public kale_device::CommandList {
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
                            kale_device::BufferHandle, std::size_t, std::size_t) override {
        copyBufferCalls_++;
    }
    void CopyBufferToTexture(kale_device::BufferHandle, std::size_t,
                             kale_device::TextureHandle, std::uint32_t,
                             std::uint32_t, std::uint32_t, std::uint32_t) override {
        copyTextureCalls_++;
    }
    void CopyTextureToTexture(kale_device::TextureHandle, kale_device::TextureHandle,
                              std::uint32_t, std::uint32_t) override {}
    void Barrier(const std::vector<kale_device::TextureHandle>&) override {}
    void ClearColor(kale_device::TextureHandle, const float[4]) override {}
    void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}

    int copyBufferCalls_ = 0;
    int copyTextureCalls_ = 0;
};

/** Mock 设备：CreateBuffer/MapBuffer 有效，Fence 可配置 signaled */
class MockStagingDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&,
                                          const void*) override {
        nextId_++;
        return kale_device::BufferHandle{nextId_};
    }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&,
                                             const void*) override {
        nextId_++;
        return kale_device::TextureHandle{nextId_};
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
        return static_cast<void*>(mappedArea_);
    }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}

    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &mockCmd_; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle fence) override {
        if (fence.IsValid())
            lastSubmittedFenceId_ = fence.id;
    }

    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override {
        nextId_++;
        return kale_device::FenceHandle{nextId_};
    }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle h) const override {
        return signaledFences_.count(h.id) != 0;
    }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }

    std::uint32_t AcquireNextImage() override {
        return kale_device::IRenderDevice::kInvalidSwapchainImageIndex;
    }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }

    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }

    void SignalFence(std::uint64_t id) { signaledFences_.insert(id); }
    std::uint64_t lastSubmittedFenceId_ = 0;
    MockCommandList mockCmd_;

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextId_ = 1;
    std::set<std::uint64_t> signaledFences_;
    char mappedArea_[256 * 1024];  /* 足够测试用 */
};

}  // namespace

static void test_allocate_null_device_returns_invalid() {
    kale::resource::StagingMemoryManager mgr(nullptr);
    kale::resource::StagingAllocation a = mgr.Allocate(1024);
    TEST_CHECK(!a.IsValid());
    TEST_CHECK(!a.buffer.IsValid());
}

static void test_allocate_zero_size_returns_invalid() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    kale::resource::StagingAllocation a = mgr.Allocate(0);
    TEST_CHECK(!a.IsValid());
}

static void test_allocate_returns_valid_and_writable() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    kale::resource::StagingAllocation a = mgr.Allocate(1024);
    TEST_CHECK(a.IsValid());
    TEST_CHECK(a.buffer.IsValid());
    TEST_CHECK(a.mappedPtr != nullptr);
    TEST_CHECK(a.size >= 1024u);
    /* 可写 */
    std::memset(a.mappedPtr, 0xAB, 1024);
}

static void test_free_immediate_recycle() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    mgr.SetPoolSize(64 * 1024);
    kale::resource::StagingAllocation a1 = mgr.Allocate(4096);
    TEST_CHECK(a1.IsValid());
    mgr.Free(a1);
    kale::resource::StagingAllocation a2 = mgr.Allocate(4096);
    TEST_CHECK(a2.IsValid());
    TEST_CHECK(a2.buffer.id == a1.buffer.id);
}

static void test_free_with_fence_delayed_recycle() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    mgr.SetPoolSize(64 * 1024);
    kale::resource::StagingAllocation a = mgr.Allocate(2048);
    TEST_CHECK(a.IsValid());
    kale_device::FenceHandle fence = kale_device::FenceHandle{99};
    mgr.Free(a, fence);
    /* 未 signal 时再分配应扩展新池（或复用其它块），ReclaimCompleted 不回收 */
    mgr.ReclaimCompleted();
    kale::resource::StagingAllocation a2 = mgr.Allocate(2048);
    TEST_CHECK(a2.IsValid());
    dev.SignalFence(99);
    mgr.ReclaimCompleted();
    kale::resource::StagingAllocation a3 = mgr.Allocate(2048);
    TEST_CHECK(a3.IsValid());
}

static void test_free_with_invalid_fence_immediate_recycle() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    kale::resource::StagingAllocation a = mgr.Allocate(1024);
    TEST_CHECK(a.IsValid());
    mgr.Free(a, kale_device::FenceHandle{});
    kale::resource::StagingAllocation a2 = mgr.Allocate(1024);
    TEST_CHECK(a2.IsValid());
    TEST_CHECK(a2.buffer.id == a.buffer.id);
}

static void test_submit_upload_buffer_to_buffer_null_cmd_adds_pending() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    kale::resource::StagingAllocation src = mgr.Allocate(512);
    TEST_CHECK(src.IsValid());
    kale_device::BufferHandle dst{100};
    mgr.SubmitUpload(nullptr, src, dst, 0);
    kale_device::FenceHandle f = mgr.FlushUploads(&dev);
    TEST_CHECK(f.IsValid());
    TEST_CHECK(dev.mockCmd_.copyBufferCalls_ == 1);
}

static void test_submit_upload_buffer_to_texture_invalid_src_noop() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    kale::resource::StagingAllocation invalid{};
    kale_device::TextureHandle dst{200};
    mgr.SubmitUpload(nullptr, invalid, dst, 0, 64, 64, 1);
    kale_device::FenceHandle f = mgr.FlushUploads(&dev);
    TEST_CHECK(!f.IsValid());
}

static void test_flush_uploads_empty_returns_invalid() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    kale_device::FenceHandle f = mgr.FlushUploads(&dev);
    TEST_CHECK(!f.IsValid());
}

static void test_flush_uploads_null_device_returns_invalid() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    kale::resource::StagingAllocation src = mgr.Allocate(256);
    mgr.SubmitUpload(nullptr, src, kale_device::BufferHandle{1}, 0);
    kale_device::FenceHandle f = mgr.FlushUploads(nullptr);
    TEST_CHECK(!f.IsValid());
}

/** phase6-6.3：cmd 非空时立即录制，不加入 pending */
static void test_submit_upload_with_cmd_immediate_record() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    kale::resource::StagingAllocation src = mgr.Allocate(256);
    TEST_CHECK(src.IsValid());
    kale_device::BufferHandle dst{100};
    dev.mockCmd_.copyBufferCalls_ = 0;
    mgr.SubmitUpload(&dev.mockCmd_, src, dst, 0);
    TEST_CHECK(dev.mockCmd_.copyBufferCalls_ == 1);
    kale_device::FenceHandle f = mgr.FlushUploads(&dev);
    TEST_CHECK(!f.IsValid());  /* pending 为空，不提交 */
    TEST_CHECK(dev.mockCmd_.copyBufferCalls_ == 1);  /* 未再录制 */
}

/** phase6-6.3：Buffer→Texture pending 路径，FlushUploads 触发 CopyBufferToTexture */
static void test_submit_upload_buffer_to_texture_pending_flush() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager mgr(&dev);
    kale::resource::StagingAllocation src = mgr.Allocate(64 * 64 * 4);
    TEST_CHECK(src.IsValid());
    kale_device::TextureHandle dst{200};
    dev.mockCmd_.copyTextureCalls_ = 0;
    mgr.SubmitUpload(nullptr, src, dst, 0, 64, 64, 1);
    kale_device::FenceHandle f = mgr.FlushUploads(&dev);
    TEST_CHECK(f.IsValid());
    TEST_CHECK(dev.mockCmd_.copyTextureCalls_ == 1);
}

int main() {
    test_allocate_null_device_returns_invalid();
    test_allocate_zero_size_returns_invalid();
    test_allocate_returns_valid_and_writable();
    test_free_immediate_recycle();
    test_free_with_fence_delayed_recycle();
    test_free_with_invalid_fence_immediate_recycle();
    test_submit_upload_buffer_to_buffer_null_cmd_adds_pending();
    test_submit_upload_buffer_to_texture_invalid_src_noop();
    test_flush_uploads_empty_returns_invalid();
    test_flush_uploads_null_device_returns_invalid();
    test_submit_upload_with_cmd_immediate_record();
    test_submit_upload_buffer_to_texture_pending_flush();
    return 0;
}
