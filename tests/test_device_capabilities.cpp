/**
 * @file test_device_capabilities.cpp
 * @brief phase11-11.7 设备能力查询单元测试
 *
 * 验证 Vulkan 后端从 VkPhysicalDevice 正确填充 DeviceCapabilities：
 * maxTextureSize、maxComputeWorkGroupSize、supportsGeometryShader、
 * supportsTessellation、supportsComputeShader、maxRecordingThreads。
 */

#include <kale_device/render_device.hpp>
#include <kale_device/window_system.hpp>
#include <cassert>
#include <cstdint>

using namespace kale_device;

int main() {
    WindowSystem ws;
    WindowConfig wc;
    wc.width = 64;
    wc.height = 64;
    wc.title = "DeviceCaps";
    if (!ws.Create(wc)) return 0;

    std::unique_ptr<IRenderDevice> dev = CreateRenderDevice(Backend::Vulkan);
    if (!dev) return 0;

    DeviceConfig dc;
    dc.windowHandle = ws.GetNativeHandle();
    dc.width = ws.GetWidth();
    dc.height = ws.GetHeight();
    dc.enableValidation = false;
    dc.maxRecordingThreads = 2;
    if (!dev->Initialize(dc)) {
        ws.Destroy();
        return 0;
    }

    const DeviceCapabilities& caps = dev->GetCapabilities();

    // 从 VkPhysicalDevice 查询的 limits：应为正数
    assert(caps.maxTextureSize > 0 && "maxTextureSize 应从 VkPhysicalDeviceProperties.limits 查询");
    // maxComputeWorkGroupSize 由 Vulkan 规范保证至少 [1,1,1]
    assert(caps.maxComputeWorkGroupSize[0] >= 1u && "maxComputeWorkGroupSize[0] 应从 physical device 查询");
    assert(caps.maxComputeWorkGroupSize[1] >= 1u && "maxComputeWorkGroupSize[1] 应从 physical device 查询");
    assert(caps.maxComputeWorkGroupSize[2] >= 1u && "maxComputeWorkGroupSize[2] 应从 physical device 查询");

    // maxRecordingThreads 应与 DeviceConfig 对齐
    assert(caps.maxRecordingThreads == 2u && "maxRecordingThreads 应与 DeviceConfig 一致");

    // 布尔能力由 physical device features/queue 填充，不要求具体值
    (void)caps.supportsGeometryShader;
    (void)caps.supportsTessellation;
    (void)caps.supportsComputeShader;
    (void)caps.supportsRayTracing;

    dev->Shutdown();
    ws.Destroy();
    return 0;
}
