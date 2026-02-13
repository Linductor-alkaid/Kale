/**
 * @file test_vulkan_scheduler_integration.cpp
 * @brief phase13-13.6 Vulkan 多线程与 RenderTaskScheduler 集成单元测试
 *
 * 验证：ParallelRecordCommands(recordFuncs, dependencies, maxThreads) 重载向各录制任务传递
 * threadIndex，且 threadIndex 在 [0, maxThreads) 内；设备层 BeginCommandList(threadIndex) 被
 * 正确调用，与 Vulkan 每线程独立 CommandPool 语义一致。
 */

#include <kale_executor/render_task_scheduler.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <executor/executor.hpp>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__       \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                               \
    } while (0)

namespace {

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
    void ClearColor(kale_device::TextureHandle, const float*) override {}
    void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
};

class ThreadIndexRecordingDevice : public kale_device::IRenderDevice {
public:
    std::vector<std::uint32_t> threadIndicesSeen;
    std::mutex mutex;
    static constexpr std::uint32_t kMaxThreads = 4;

    bool Initialize(const kale_device::DeviceConfig&) override {
        caps_.maxRecordingThreads = kMaxThreads;
        return true;
    }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override { return {}; }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override { return {}; }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }
    kale_device::DescriptorSetHandle CreateDescriptorSet(const kale_device::DescriptorSetLayoutDesc&) override { return {}; }
    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::BufferHandle, std::size_t, std::size_t) override {}
    void DestroyBuffer(kale_device::BufferHandle) override {}
    void DestroyTexture(kale_device::TextureHandle) override {}
    void DestroyShader(kale_device::ShaderHandle) override {}
    void DestroyPipeline(kale_device::PipelineHandle) override {}
    void DestroyDescriptorSet(kale_device::DescriptorSetHandle) override {}
    void UpdateBuffer(kale_device::BufferHandle, const void*, std::size_t, std::size_t) override {}
    void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override { return nullptr; }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}

    kale_device::CommandList* BeginCommandList(std::uint32_t threadIndex) override {
        std::lock_guard<std::mutex> lock(mutex);
        threadIndicesSeen.push_back(threadIndex);
        return &mockCmd_;
    }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}
    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override { return {}; }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }
    std::uint32_t AcquireNextImage() override { return 0; }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }
    void SetExtent(std::uint32_t, std::uint32_t) override {}

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    MockCmd mockCmd_;
};

/** ParallelRecordCommands(recordFuncs, deps, maxThreads) 向每个任务传递 threadIndex，且 threadIndex < maxThreads */
static void test_parallel_record_commands_with_thread_index() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    ThreadIndexRecordingDevice dev;
    TEST_CHECK(dev.Initialize({}));

    // 3 个无依赖的“Pass”，应并行执行，收到 threadIndex 0, 1, 2（maxThreads=4）
    std::vector<std::function<void(std::uint32_t)>> recordFuncs;
    std::vector<std::vector<size_t>> dependencies(3);
    for (int i = 0; i < 3; ++i)
        recordFuncs.push_back([&dev](std::uint32_t threadIndex) {
            kale_device::CommandList* cmd = dev.BeginCommandList(threadIndex);
            if (cmd) dev.EndCommandList(cmd);
        });

    sched.ParallelRecordCommands(std::move(recordFuncs), dependencies, dev.GetCapabilities().maxRecordingThreads);

    TEST_CHECK(dev.threadIndicesSeen.size() == 3u);
    for (std::uint32_t ti : dev.threadIndicesSeen) {
        TEST_CHECK(ti < ThreadIndexRecordingDevice::kMaxThreads);
    }
    // 同一层内应收到 0, 1, 2（顺序不定）
    std::vector<std::uint32_t> expected = {0, 1, 2};
    std::sort(dev.threadIndicesSeen.begin(), dev.threadIndicesSeen.end());
    TEST_CHECK(dev.threadIndicesSeen == expected);
}

/** 两层依赖：第一层 2 个 Pass（threadIndex 0,1），第二层 1 个 Pass（threadIndex 0） */
static void test_parallel_record_commands_two_levels() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    ThreadIndexRecordingDevice dev;
    TEST_CHECK(dev.Initialize({}));

    std::vector<std::function<void(std::uint32_t)>> recordFuncs(3);
    std::vector<std::vector<size_t>> dependencies(3);
    dependencies[0] = {};
    dependencies[1] = {};
    dependencies[2] = {0, 1};  // pass 2 依赖 pass 0 和 1

    recordFuncs[0] = [&dev](std::uint32_t ti) {
        kale_device::CommandList* cmd = dev.BeginCommandList(ti);
        if (cmd) dev.EndCommandList(cmd);
    };
    recordFuncs[1] = [&dev](std::uint32_t ti) {
        kale_device::CommandList* cmd = dev.BeginCommandList(ti);
        if (cmd) dev.EndCommandList(cmd);
    };
    recordFuncs[2] = [&dev](std::uint32_t ti) {
        kale_device::CommandList* cmd = dev.BeginCommandList(ti);
        if (cmd) dev.EndCommandList(cmd);
    };

    sched.ParallelRecordCommands(std::move(recordFuncs), dependencies, 4u);

    TEST_CHECK(dev.threadIndicesSeen.size() == 3u);
    for (std::uint32_t ti : dev.threadIndicesSeen)
        TEST_CHECK(ti < 4u);
    // 第一层两个应得 0 和 1；第二层一个得 0
    TEST_CHECK(dev.threadIndicesSeen[0] < 4u && dev.threadIndicesSeen[1] < 4u && dev.threadIndicesSeen[2] < 4u);
}

/** maxThreads=2 时同一层 3 个 Pass 分两批：先 0,1 再 0 */
static void test_parallel_record_commands_chunked() {
    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    ThreadIndexRecordingDevice dev;
    TEST_CHECK(dev.Initialize({}));

    std::vector<std::function<void(std::uint32_t)>> recordFuncs(3);
    std::vector<std::vector<size_t>> dependencies(3);

    for (size_t i = 0; i < 3; ++i) {
        recordFuncs[i] = [&dev](std::uint32_t ti) {
            kale_device::CommandList* cmd = dev.BeginCommandList(ti);
            if (cmd) dev.EndCommandList(cmd);
        };
    }

    sched.ParallelRecordCommands(std::move(recordFuncs), dependencies, 2u);

    TEST_CHECK(dev.threadIndicesSeen.size() == 3u);
    for (std::uint32_t ti : dev.threadIndicesSeen)
        TEST_CHECK(ti < 2u);
}

}  // namespace

int main() {
    test_parallel_record_commands_with_thread_index();
    test_parallel_record_commands_two_levels();
    test_parallel_record_commands_chunked();
    std::cout << "test_vulkan_scheduler_integration: all passed" << std::endl;
    return 0;
}
