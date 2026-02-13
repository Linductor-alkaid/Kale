/**
 * @file test_vma_integration.cpp
 * @brief phase13-13.5 VMA 集成单元测试
 *
 * 验证 Vulkan 后端使用 VMA 分配 Buffer/Texture 时：
 * CreateBuffer、CreateTexture 返回有效句柄，Destroy 不崩溃，Shutdown 正确释放。
 */

#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/window_system.hpp>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>

using namespace kale_device;

int main() {
    WindowSystem ws;
    WindowConfig wc;
    wc.width = 64;
    wc.height = 64;
    wc.title = "VMA";
    if (!ws.Create(wc)) return 0;

    std::unique_ptr<IRenderDevice> dev = CreateRenderDevice(Backend::Vulkan);
    if (!dev) return 0;

    DeviceConfig dc;
    dc.windowHandle = ws.GetNativeHandle();
    dc.width = ws.GetWidth();
    dc.height = ws.GetHeight();
    dc.enableValidation = false;
    if (!dev->Initialize(dc)) {
        ws.Destroy();
        return 0;
    }

    // Buffer: device-local，无初始数据（Staging 由上层使用）
    BufferDesc bufDesc = {};
    bufDesc.size = 256;
    bufDesc.usage = BufferUsage::Vertex;
    bufDesc.cpuVisible = false;
    BufferHandle buf = dev->CreateBuffer(bufDesc, nullptr);
    assert(buf.IsValid() && "CreateBuffer(device-local) 应返回有效句柄");
    dev->DestroyBuffer(buf);

    // Buffer: CPU visible，带初始数据（VMA 或原生路径均应支持）
    bufDesc.cpuVisible = true;
    uint8_t data[64];
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = static_cast<uint8_t>(i);
    buf = dev->CreateBuffer(bufDesc, data);
    assert(buf.IsValid() && "CreateBuffer(cpuVisible, data) 应返回有效句柄");
    void* mapped = dev->MapBuffer(buf, 0, 64);
    assert(mapped && "MapBuffer 应返回非空指针");
    assert(std::memcmp(mapped, data, 64) == 0 && "映射内容应与初始数据一致");
    dev->UnmapBuffer(buf);
    dev->DestroyBuffer(buf);

    // Texture: 小尺寸，无初始数据
    TextureDesc texDesc = {};
    texDesc.width = 4;
    texDesc.height = 4;
    texDesc.depth = 1;
    texDesc.format = Format::RGBA8_UNORM;
    texDesc.usage = TextureUsage::Sampled;
    texDesc.mipLevels = 1;
    texDesc.arrayLayers = 1;
    TextureHandle tex = dev->CreateTexture(texDesc, nullptr);
    assert(tex.IsValid() && "CreateTexture 应返回有效句柄");
    dev->DestroyTexture(tex);

    // Texture: 带初始数据（4x4 RGBA8）
    uint8_t pixels[4 * 4 * 4];
    std::memset(pixels, 0x80, sizeof(pixels));
    tex = dev->CreateTexture(texDesc, pixels);
    assert(tex.IsValid() && "CreateTexture(带数据) 应返回有效句柄");
    dev->DestroyTexture(tex);

    dev->Shutdown();
    ws.Destroy();
    return 0;
}
