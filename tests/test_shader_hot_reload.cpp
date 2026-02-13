/**
 * @file test_shader_hot_reload.cpp
 * @brief phase10-10.9 着色器热重载集成单元测试
 *
 * 覆盖：EnableHotReload/IsHotReloadEnabled；ProcessHotReload 禁用时不操作、不崩溃；
 * LoadShader 后记录 path 的 mtime；文件 mtime 变更后 ProcessHotReload 调用 ReloadShader 并派发 RegisterReloadCallback。
 */

#include <kale_pipeline/shader_manager.hpp>
#include <kale_resource/shader_compiler.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

namespace {

std::vector<std::uint8_t> MinimalSPIRV() {
    std::vector<std::uint8_t> out;
    out.push_back(0x03); out.push_back(0x02); out.push_back(0x23); out.push_back(0x07);
    out.push_back(0x00); out.push_back(0x00); out.push_back(0x01); out.push_back(0x00);
    out.push_back(0x00); out.push_back(0x00); out.push_back(0x00); out.push_back(0x00);
    out.push_back(0x00); out.push_back(0x00); out.push_back(0x00); out.push_back(0x00);
    out.push_back(0x00); out.push_back(0x00); out.push_back(0x00); out.push_back(0x00);
    return out;
}

class MockShaderDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }
    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override { return {}; }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override { return {}; }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc& desc) override {
        if (desc.code.empty()) return {};
        nextShaderId_++;
        return kale_device::ShaderHandle{nextShaderId_};
    }
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
    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return nullptr; }
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
    kale_device::DescriptorSetHandle AcquireInstanceDescriptorSet(const void*, std::size_t) override { return {}; }
    void ReleaseInstanceDescriptorSet(kale_device::DescriptorSetHandle) override {}

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_{};
    std::uint64_t nextShaderId_ = 0;
};

}  // namespace

int main() {
    using namespace kale::pipeline;
    using namespace kale::resource;

    ShaderManager mgr;

    // EnableHotReload / IsHotReloadEnabled
    TEST_CHECK(!mgr.IsHotReloadEnabled());
    mgr.EnableHotReload(true);
    TEST_CHECK(mgr.IsHotReloadEnabled());
    mgr.EnableHotReload(false);
    TEST_CHECK(!mgr.IsHotReloadEnabled());

    // ProcessHotReload 禁用时不操作、不崩溃
    mgr.EnableHotReload(false);
    mgr.ProcessHotReload();
    mgr.ProcessHotReload();

    // 无任何加载时 ProcessHotReload 启用也不崩溃
    mgr.EnableHotReload(true);
    mgr.ProcessHotReload();

    // 准备临时 .spv（绝对路径，避免 cwd 差异）
    std::string tmpPath = (std::filesystem::current_path() / "test_shader_hot_reload_minimal.spv").string();
    {
        std::ofstream f(tmpPath, std::ios::binary);
        std::vector<std::uint8_t> code = MinimalSPIRV();
        f.write(reinterpret_cast<const char*>(code.data()), code.size());
    }
    // 先 sleep 再 LoadShader，确保首次 mtime 与后续写入落在不同秒（1s 精度文件系统）
    std::this_thread::sleep_for(std::chrono::seconds(2));
    ShaderCompiler compiler;
    MockShaderDevice dev;
    mgr.SetCompiler(&compiler);
    mgr.SetDevice(&dev);

    kale_device::ShaderHandle h = mgr.LoadShader(tmpPath, kale_device::ShaderStage::Vertex, &dev);
    TEST_CHECK(h.IsValid());

    // 注册回调：记录被重载的 path
    std::vector<std::string> reloadedPaths;
    mgr.RegisterReloadCallback([&reloadedPaths](const std::string& path) {
        reloadedPaths.push_back(path);
    });

    mgr.EnableHotReload(true);
    reloadedPaths.clear();
    mgr.ProcessHotReload();
    TEST_CHECK(reloadedPaths.empty());

    // 再次写文件以更新 mtime（模拟外部修改）
    {
        std::ofstream f(tmpPath.c_str(), std::ios::binary);
        std::vector<std::uint8_t> code = MinimalSPIRV();
        code.push_back(0x00);
        f.write(reinterpret_cast<const char*>(code.data()), code.size());
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));

    mgr.ProcessHotReload();
    TEST_CHECK(reloadedPaths.size() == 1u && reloadedPaths[0] == tmpPath);

    // 再次 ProcessHotReload 无变化，不应再次回调
    reloadedPaths.clear();
    mgr.ProcessHotReload();
    TEST_CHECK(reloadedPaths.empty());

    std::remove(tmpPath.c_str());
    std::cout << "test_shader_hot_reload passed\n";
    return 0;
}
