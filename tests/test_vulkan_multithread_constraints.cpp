/**
 * @file test_vulkan_multithread_constraints.cpp
 * @brief phase9-9.6 Vulkan 多线程录制约束单元测试
 *
 * 验证三项约束：
 * 1) 每 VkCommandBuffer 单线程录制：不同 threadIndex 得到不同 CommandList，无共享 buffer。
 * 2) 每线程使用独立 CommandPool：RDI 按 threadIndex 分配，Vulkan 后端每 threadIndex 对应独立 Pool。
 * 3) RDI 的 BeginCommandList(threadIndex) 支持多线程：有效 threadIndex 返回非空，越界返回 nullptr；
 *    GetCapabilities().maxRecordingThreads 与 device_abstraction_layer 对齐。
 */

#include <kale_device/render_device.hpp>
#include <kale_device/window_system.hpp>
#include <cassert>
#include <cstdint>
#include <set>
#include <vector>

using namespace kale_device;

namespace {

void test_constraint_one_buffer_per_thread() {
    // 约束 1：不同 threadIndex 必须得到不同 CommandList（即不同 VkCommandBuffer），保证单线程录制。
    WindowSystem ws;
    WindowConfig wc;
    wc.width = 64;
    wc.height = 64;
    wc.title = "Constraint1";
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

    std::set<CommandList*> unique;
    for (std::uint32_t ti = 0; ti < 4; ++ti) {
        CommandList* cmd = dev->BeginCommandList(ti);
        assert(cmd && "BeginCommandList(threadIndex) 应返回非空");
        assert(unique.find(cmd) == unique.end() && "不同 threadIndex 必须对应不同 CommandList（每 buffer 单线程）");
        unique.insert(cmd);
        dev->EndCommandList(cmd);
    }
    assert(unique.size() == 4u);

    dev->Shutdown();
    ws.Destroy();
}

void test_constraint_per_thread_command_pool() {
    // 约束 2：每线程独立 CommandPool。通过 RDI 表现为：不同 threadIndex 得到不同 CommandList，
    // 且 Submit 时多份 CommandList 按序合并无冲突。
    WindowSystem ws;
    WindowConfig wc;
    wc.width = 64;
    wc.height = 64;
    wc.title = "Constraint2";
    if (!ws.Create(wc)) return;

    std::unique_ptr<IRenderDevice> dev = CreateRenderDevice(Backend::Vulkan);
    if (!dev) return;

    DeviceConfig dc;
    dc.windowHandle = ws.GetNativeHandle();
    dc.width = ws.GetWidth();
    dc.height = ws.GetHeight();
    dc.enableValidation = false;
    dc.maxRecordingThreads = 3;
    if (!dev->Initialize(dc)) {
        ws.Destroy();
        return;
    }

    std::vector<CommandList*> lists;
    for (std::uint32_t ti = 0; ti < 3; ++ti) {
        CommandList* cmd = dev->BeginCommandList(ti);
        assert(cmd);
        lists.push_back(cmd);
        dev->EndCommandList(cmd);
    }
    FenceHandle fence = dev->CreateFence(true);
    assert(fence.IsValid());
    dev->Submit(lists, {}, {}, fence);
    dev->WaitForFence(fence);

    dev->Shutdown();
    ws.Destroy();
}

void test_constraint_rdi_begin_command_list_multithread() {
    // 约束 3：RDI BeginCommandList(threadIndex) 支持多线程；threadIndex < maxRecordingThreads 有效，
    // threadIndex >= maxRecordingThreads 返回 nullptr；GetCapabilities 与配置对齐。
    WindowSystem ws;
    WindowConfig wc;
    wc.width = 64;
    wc.height = 64;
    wc.title = "Constraint3";
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

    kale_device::DeviceCapabilities caps = dev->GetCapabilities();
    assert(caps.maxRecordingThreads == 2u && "GetCapabilities().maxRecordingThreads 应与 DeviceConfig 对齐");

    assert(dev->BeginCommandList(0) != nullptr);
    assert(dev->BeginCommandList(1) != nullptr);
    assert(dev->BeginCommandList(2) == nullptr && "threadIndex >= maxRecordingThreads 应返回 nullptr");
    assert(dev->BeginCommandList(100) == nullptr);

    dev->Shutdown();
    ws.Destroy();
}

}  // namespace

int main() {
    test_constraint_one_buffer_per_thread();
    test_constraint_per_thread_command_pool();
    test_constraint_rdi_begin_command_list_multithread();
    return 0;
}
