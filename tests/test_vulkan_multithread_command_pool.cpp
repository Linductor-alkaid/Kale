/**
 * @file test_vulkan_multithread_command_pool.cpp
 * @brief phase9-9.1 Vulkan 多线程 CommandPool 单元测试
 *
 * 验证：DeviceConfig.maxRecordingThreads 生效；每线程独立 CommandPool；
 * BeginCommandList(threadIndex) 从对应池分配；多 CommandList Submit 按序合并无崩溃。
 */

#include <kale_device/render_device.hpp>
#include <kale_device/window_system.hpp>
#include <cassert>
#include <cstdint>
#include <thread>
#include <vector>

using namespace kale_device;

namespace {

void test_config_max_recording_threads_default() {
    DeviceConfig config;
    assert(config.maxRecordingThreads >= 1u);
}

void test_multiple_pools_begin_end_submit() {
    WindowSystem ws;
    WindowConfig wc;
    wc.width = 64;
    wc.height = 64;
    wc.title = "MultiPool";
    if (!ws.Create(wc)) return;

    std::unique_ptr<IRenderDevice> dev = CreateRenderDevice(Backend::Vulkan);
    if (!dev) return;

    DeviceConfig dc;
    dc.windowHandle = ws.GetNativeHandle();
    dc.width = ws.GetWidth();
    dc.height = ws.GetHeight();
    dc.enableValidation = false;
    dc.maxRecordingThreads = 4;
    if (!dev->Initialize(dc)) {
        ws.Destroy();
        return;
    }

    std::vector<CommandList*> lists;
    lists.reserve(4);
    for (std::uint32_t ti = 0; ti < 4; ++ti) {
        CommandList* cmd = dev->BeginCommandList(ti);
        assert(cmd && "BeginCommandList(threadIndex) 应返回非空");
        lists.push_back(cmd);
    }
    assert(lists[0] != lists[1] && "不同 threadIndex 应得到不同 CommandList");
    for (CommandList* c : lists)
        dev->EndCommandList(c);

    FenceHandle fence = dev->CreateFence(true);
    assert(fence.IsValid());
    dev->Submit(lists, {}, {}, fence);
    dev->WaitForFence(fence);

    dev->Shutdown();
    ws.Destroy();
}

void test_out_of_range_returns_null() {
    WindowSystem ws;
    WindowConfig wc;
    wc.width = 64;
    wc.height = 64;
    wc.title = "OOB";
    if (!ws.Create(wc)) return;

    std::unique_ptr<IRenderDevice> dev = CreateRenderDevice(Backend::Vulkan);
    if (!dev) return;

    DeviceConfig dc;
    dc.windowHandle = ws.GetNativeHandle();
    dc.width = ws.GetWidth();
    dc.height = ws.GetHeight();
    dc.enableValidation = false;
    dc.maxRecordingThreads = 2;
    if (!dev->Initialize(dc)) {
        ws.Destroy();
        return;
    }

    CommandList* c0 = dev->BeginCommandList(0);
    CommandList* c1 = dev->BeginCommandList(1);
    assert(c0 != nullptr && c1 != nullptr);
    CommandList* oob = dev->BeginCommandList(2);
    assert(oob == nullptr && "threadIndex >= maxRecordingThreads 应返回 nullptr");
    dev->EndCommandList(c0);
    dev->EndCommandList(c1);

    dev->Shutdown();
    ws.Destroy();
}

void test_single_thread_pool_still_works() {
    WindowSystem ws;
    WindowConfig wc;
    wc.width = 64;
    wc.height = 64;
    wc.title = "Single";
    if (!ws.Create(wc)) return;

    std::unique_ptr<IRenderDevice> dev = CreateRenderDevice(Backend::Vulkan);
    if (!dev) return;

    DeviceConfig dc;
    dc.windowHandle = ws.GetNativeHandle();
    dc.width = ws.GetWidth();
    dc.height = ws.GetHeight();
    dc.enableValidation = false;
    dc.maxRecordingThreads = 1;
    if (!dev->Initialize(dc)) {
        ws.Destroy();
        return;
    }

    CommandList* c0 = dev->BeginCommandList(0);
    assert(c0 != nullptr);
    assert(dev->BeginCommandList(1) == nullptr);
    dev->EndCommandList(c0);
    dev->Shutdown();
    ws.Destroy();
}

}  // namespace

int main() {
    test_config_max_recording_threads_default();
    test_multiple_pools_begin_end_submit();
    test_out_of_range_returns_null();
    test_single_thread_pool_still_works();
    return 0;
}
