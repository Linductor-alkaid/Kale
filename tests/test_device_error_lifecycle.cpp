/**
 * @file test_device_error_lifecycle.cpp
 * @brief phase13-13.10 设备抽象层错误处理与生命周期单元测试
 *
 * 验证：Initialize 失败时 GetLastError() 返回详细原因；
 * 无效句柄调用 Destroy* 不崩溃；
 * 窗口 resize/OUT_OF_DATE 与最小化策略由现有实现与 Run 覆盖。
 */

#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/window_system.hpp>

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>

using namespace kale_device;

int main() {
    // 1) Initialize 失败时 GetLastError() 返回非空
    std::unique_ptr<IRenderDevice> dev = CreateRenderDevice(Backend::Vulkan);
    assert(dev && "CreateRenderDevice(Vulkan) 应非空");

    DeviceConfig invalidConfig;
    invalidConfig.windowHandle = nullptr;
    invalidConfig.width = 0;
    invalidConfig.height = 0;
    bool initOk = dev->Initialize(invalidConfig);
    assert(!initOk && "无效 config (null window, 0x0) 应导致 Initialize 失败");
    const std::string& err = dev->GetLastError();
    assert(!err.empty() && "Initialize 失败后 GetLastError() 应返回非空详细原因");

    dev->Shutdown();

    // 2) 有效初始化后，无效句柄调用 Destroy* 不崩溃（调用方不得使用无效句柄；实现应防御性 no-op）
    WindowSystem ws;
    kale_device::WindowConfig wc;
    wc.width = 64;
    wc.height = 64;
    wc.title = "Lifecycle";
    if (!ws.Create(wc)) return 0;

    dev = CreateRenderDevice(Backend::Vulkan);
    if (!dev) {
        ws.Destroy();
        return 0;
    }
    DeviceConfig validConfig;
    validConfig.windowHandle = ws.GetNativeHandle();
    validConfig.width = ws.GetWidth();
    validConfig.height = ws.GetHeight();
    if (!dev->Initialize(validConfig)) {
        ws.Destroy();
        return 0;
    }

    BufferHandle invalidBuf;
    TextureHandle invalidTex;
    ShaderHandle invalidSh;
    PipelineHandle invalidPipe;
    DescriptorSetHandle invalidSet;
    assert(!invalidBuf.IsValid() && !invalidTex.IsValid());

    dev->DestroyBuffer(invalidBuf);
    dev->DestroyTexture(invalidTex);
    dev->DestroyShader(invalidSh);
    dev->DestroyPipeline(invalidPipe);
    dev->DestroyDescriptorSet(invalidSet);
    dev->ReleaseInstanceDescriptorSet(invalidSet);

    dev->Shutdown();
    ws.Destroy();
    return 0;
}
