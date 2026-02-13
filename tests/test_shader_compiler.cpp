/**
 * @file test_shader_compiler.cpp
 * @brief phase8-8.3 ShaderCompiler 单元测试
 *
 * 覆盖：StageFromExtension、SupportsExtension；ResolvePath；LoadSPIRV（成功/失败）；
 * Compile null device、Compile 不支持的扩展名；Compile 有效 .spv + mock 设备返回有效句柄；
 * GetLastError、Recompile 与 Compile 一致。
 */

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

/** 最小合法 SPIR-V 头（magic + version + generator + bound + reserved） */
std::vector<std::uint8_t> MinimalSPIRV() {
    std::vector<std::uint8_t> out;
    out.push_back(0x03); out.push_back(0x02); out.push_back(0x23); out.push_back(0x07);  // magic
    out.push_back(0x00); out.push_back(0x00); out.push_back(0x01); out.push_back(0x00);  // version 1.0
    out.push_back(0x00); out.push_back(0x00); out.push_back(0x00); out.push_back(0x00);  // generator
    out.push_back(0x00); out.push_back(0x00); out.push_back(0x00); out.push_back(0x00);  // bound
    out.push_back(0x00); out.push_back(0x00); out.push_back(0x00); out.push_back(0x00);  // reserved
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

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_{};
    uint32_t nextShaderId_ = 0;
};

}  // namespace

int main() {
    using namespace kale::resource;
    using namespace kale_device;

    // StageFromExtension
    TEST_CHECK(ShaderCompiler::StageFromExtension("a.vert") == ShaderStage::Vertex);
    TEST_CHECK(ShaderCompiler::StageFromExtension("b.frag") == ShaderStage::Fragment);
    TEST_CHECK(ShaderCompiler::StageFromExtension("c.comp") == ShaderStage::Compute);
    TEST_CHECK(ShaderCompiler::StageFromExtension("d.spv") == ShaderStage::Vertex);  // default
    TEST_CHECK(ShaderCompiler::StageFromExtension("e.UNKNOWN") == ShaderStage::Vertex);

    // SupportsExtension
    TEST_CHECK(ShaderCompiler::SupportsExtension("a.vert"));
    TEST_CHECK(ShaderCompiler::SupportsExtension("b.frag"));
    TEST_CHECK(ShaderCompiler::SupportsExtension("c.comp"));
    TEST_CHECK(ShaderCompiler::SupportsExtension("d.spv"));
    TEST_CHECK(!ShaderCompiler::SupportsExtension("e.glsl"));
    TEST_CHECK(!ShaderCompiler::SupportsExtension("noext"));

    // ResolvePath
    ShaderCompiler compiler;
    TEST_CHECK(compiler.ResolvePath("shaders/tri.vert") == "shaders/tri.vert");
    compiler.SetBasePath("/assets");
    TEST_CHECK(compiler.ResolvePath("shaders/tri.vert") == "/assets/shaders/tri.vert");
    TEST_CHECK(compiler.GetBasePath() == "/assets");

    // LoadSPIRV: 写入临时 .spv 后加载
    compiler.SetBasePath("");
    std::string spvPath = "kale_test_shader_compiler.spv";  // 相对 cwd（build 或 tests 目录）
    {
        std::vector<std::uint8_t> minimal = MinimalSPIRV();
        std::ofstream f(spvPath, std::ios::binary);
        TEST_CHECK(f);
        f.write(reinterpret_cast<const char*>(minimal.data()), minimal.size());
        f.close();
    }
    std::vector<std::uint8_t> loaded;
    TEST_CHECK(compiler.LoadSPIRV(spvPath, loaded));
    TEST_CHECK(loaded.size() == 20u);
    TEST_CHECK(compiler.GetLastError().empty());

    // LoadSPIRV: 不存在的文件
    std::vector<std::uint8_t> emptyLoaded;
    TEST_CHECK(!compiler.LoadSPIRV("nonexistent_path_12345.spv", emptyLoaded));
    TEST_CHECK(emptyLoaded.empty());
    TEST_CHECK(!compiler.GetLastError().empty());

    // Compile with null device
    kale_device::ShaderHandle nullHandle = compiler.Compile(spvPath, ShaderStage::Vertex, nullptr);
    TEST_CHECK(!nullHandle.IsValid());
    TEST_CHECK(!compiler.GetLastError().empty());

    // Compile with unsupported extension
    MockShaderDevice mockDev;
    compiler.SetBasePath("");
    kale_device::ShaderHandle badExt = compiler.Compile("/tmp/foo.xyz", ShaderStage::Vertex, &mockDev);
    TEST_CHECK(!badExt.IsValid());
    TEST_CHECK(!compiler.GetLastError().empty());

    // Compile valid .spv with mock device
    kale_device::ShaderHandle h = compiler.Compile(spvPath, ShaderStage::Vertex, &mockDev);
    TEST_CHECK(h.IsValid());
    TEST_CHECK(compiler.GetLastError().empty());

    // Recompile same path
    kale_device::ShaderHandle h2 = compiler.Recompile(spvPath, ShaderStage::Vertex, &mockDev);
    TEST_CHECK(h2.IsValid());
    TEST_CHECK(h2.id != h.id);  // new handle

    (void)std::remove(spvPath.c_str());
    return 0;
}
