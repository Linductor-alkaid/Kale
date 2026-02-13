/**
 * @file test_shader_manager.cpp
 * @brief phase10-10.8 ShaderManager 单元测试
 *
 * 覆盖：LoadShader null compiler/device 返回无效；GetShader 未知 name 返回无效；
 * LoadShader 有效路径+compiler+device 返回有效句柄并缓存；GetShader 用 MakeCacheKey 查到同一句柄；
 * MakeCacheKey 格式；ReloadShader 在 SetDevice/SetCompiler 下对匹配 path 的项调用 Destroy+Recompile。
 */

#include <kale_pipeline/shader_manager.hpp>
#include <kale_resource/shader_compiler.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
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

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override {
        return {};
    }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override {
        return {};
    }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc& desc) override {
        if (desc.code.empty()) return {};
        nextShaderId_++;
        return kale_device::ShaderHandle{nextShaderId_};
    }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override {
        return {};
    }
    kale_device::DescriptorSetHandle CreateDescriptorSet(
        const kale_device::DescriptorSetLayoutDesc&) override {
        return {};
    }
    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t,
                                   kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t,
                                  kale_device::BufferHandle, std::size_t, std::size_t) override {}
    void DestroyBuffer(kale_device::BufferHandle) override {}
    void DestroyTexture(kale_device::TextureHandle) override {}
    void DestroyShader(kale_device::ShaderHandle h) override {
        destroyShaderCount_++;
        lastDestroyedId_ = h.id;
    }
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
    void WaitForFence(kale_device::FenceHandle, std::uint64_t = UINT64_MAX) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }
    std::uint32_t AcquireNextImage() override { return 0; }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }
    void SetExtent(std::uint32_t, std::uint32_t) override {}

    int destroyShaderCount_ = 0;
    std::uint64_t lastDestroyedId_ = 0;

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_{};
    std::uint64_t nextShaderId_ = 0;
};

}  // namespace

int main() {
    using namespace kale::pipeline;
    using namespace kale::resource;

    // MakeCacheKey 格式
    std::string key = ShaderManager::MakeCacheKey("mesh.vert", kale_device::ShaderStage::Vertex);
    TEST_CHECK(key == "mesh.vert|Vertex");
    key = ShaderManager::MakeCacheKey("pbr.frag", kale_device::ShaderStage::Fragment);
    TEST_CHECK(key == "pbr.frag|Fragment");

    ShaderManager mgr;
    MockShaderDevice dev;

    // GetShader 未知 name 返回无效
    TEST_CHECK(!mgr.GetShader("unknown").IsValid());

    // LoadShader null device 返回无效
    kale_device::ShaderHandle h = mgr.LoadShader("any.vert", kale_device::ShaderStage::Vertex, nullptr);
    TEST_CHECK(!h.IsValid());
    TEST_CHECK(!mgr.GetLastError().empty());

    // LoadShader null compiler 返回无效（不设置 compiler）
    mgr.SetDevice(&dev);
    h = mgr.LoadShader("any.vert", kale_device::ShaderStage::Vertex, &dev);
    TEST_CHECK(!h.IsValid());

    // 准备 ShaderCompiler + 临时 .spv 文件
    ShaderCompiler compiler;
    const char* tmpPath = "test_shader_manager_minimal.spv";
    {
        std::ofstream f(tmpPath, std::ios::binary);
        std::vector<std::uint8_t> code = MinimalSPIRV();
        f.write(reinterpret_cast<const char*>(code.data()), code.size());
    }
    mgr.SetCompiler(&compiler);

    // LoadShader 有效路径 + compiler + device 返回有效句柄
    h = mgr.LoadShader(tmpPath, kale_device::ShaderStage::Vertex, &dev);
    TEST_CHECK(h.IsValid());

    // GetShader 用 MakeCacheKey 查到同一句柄
    std::string cacheKey = ShaderManager::MakeCacheKey(tmpPath, kale_device::ShaderStage::Vertex);
    kale_device::ShaderHandle h2 = mgr.GetShader(cacheKey);
    TEST_CHECK(h2.IsValid() && h2.id == h.id);

    // 再次 LoadShader 同一 path+stage 返回缓存句柄（不重复 Create）
    kale_device::ShaderHandle h3 = mgr.LoadShader(tmpPath, kale_device::ShaderStage::Vertex, &dev);
    TEST_CHECK(h3.IsValid() && h3.id == h.id);

    // ReloadShader：SetDevice 已设，应对该 path 下条目调用 Destroy 并 Recompile
    mgr.SetDevice(&dev);
    int destroyBefore = dev.destroyShaderCount_;
    mgr.ReloadShader(tmpPath);
    TEST_CHECK(dev.destroyShaderCount_ > destroyBefore);
    kale_device::ShaderHandle afterReload = mgr.GetShader(cacheKey);
    TEST_CHECK(afterReload.IsValid());

    // ReloadShader 无 compiler 时设置 GetLastError
    ShaderManager mgr2;
    mgr2.SetDevice(&dev);
    mgr2.ReloadShader("nonexistent.spv");
    TEST_CHECK(!mgr2.GetLastError().empty());

    std::remove(tmpPath);
    std::cout << "test_shader_manager passed\n";
    return 0;
}
