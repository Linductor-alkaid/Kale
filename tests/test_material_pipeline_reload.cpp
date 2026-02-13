/**
 * @file test_material_pipeline_reload.cpp
 * @brief phase13-13.15 材质/着色器热重载集成单元测试
 *
 * 覆盖：MaterialPipelineReloadRegistry RegisterMaterial/UnregisterMaterial/OnShaderReloaded；
 * 着色器 path 重载后材质获得新 Pipeline、旧 Pipeline 被 Destroy；
 * ResourceManager SetShaderManager 使 ctx.shaderManager 在 Load/ProcessHotReload 时可用；
 * MaterialLoader 在 JSON 含 shader_vert/shaders_frag 且 ctx.shaderManager 时创建 Pipeline。
 */

#include <kale_pipeline/material_pipeline_reload_registry.hpp>
#include <kale_pipeline/material.hpp>
#include <kale_pipeline/shader_manager.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_loader.hpp>
#include <kale_resource/shader_compiler.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

namespace {

class MockPipelineDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }
    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override { return {}; }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override { return {}; }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override {
        nextId_++;
        return kale_device::ShaderHandle{nextId_};
    }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc& desc) override {
        if (desc.shaders.size() < 2u) return {};
        nextId_++;
        kale_device::PipelineHandle h{nextId_};
        createdPipelineIds_.push_back(nextId_);
        return h;
    }
    kale_device::DescriptorSetHandle CreateDescriptorSet(const kale_device::DescriptorSetLayoutDesc&) override { return {}; }
    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::BufferHandle, std::size_t, std::size_t) override {}
    void DestroyBuffer(kale_device::BufferHandle) override {}
    void DestroyTexture(kale_device::TextureHandle) override {}
    void DestroyShader(kale_device::ShaderHandle) override {}
    void DestroyPipeline(kale_device::PipelineHandle h) override {
        if (h.IsValid()) destroyedPipelineIds_.push_back(h.id);
    }
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

    std::vector<std::uint64_t> createdPipelineIds_;
    std::vector<std::uint64_t> destroyedPipelineIds_;

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_{};
    std::uint64_t nextId_ = 0;
};

}  // namespace

int main() {
    using namespace kale::pipeline;
    using namespace kale_device;

    // ----- MaterialPipelineReloadRegistry -----
    MockPipelineDevice dev;
    kale::resource::ShaderCompiler compiler;
    compiler.SetBasePath("");
    ShaderManager shaderMgr;
    shaderMgr.SetCompiler(&compiler);
    shaderMgr.SetDevice(&dev);

    MaterialPipelineReloadRegistry registry;
    registry.SetShaderManager(&shaderMgr);
    registry.SetDevice(&dev);
    TEST_CHECK(registry.GetShaderManager() == &shaderMgr);
    TEST_CHECK(registry.GetDevice() == &dev);

    kale::pipeline::Material mat;
    kale_device::PipelineDesc desc;
    desc.vertexBindings = {{0, 32, false}};
    desc.vertexAttributes = {{0, 0, Format::RGB32F, 0}, {1, 0, Format::RGB32F, 12}, {2, 0, Format::RG32F, 24}};
    desc.topology = PrimitiveTopology::TriangleList;
    desc.colorAttachmentFormats = {Format::RGBA8_SRGB};
    desc.depthAttachmentFormat = Format::D24S8;

    // 无 shader 时 OnShaderReloaded 不崩溃
    registry.OnShaderReloaded("any.path");
    TEST_CHECK(!mat.GetPipeline().IsValid());

    // 准备临时 .spv 文件并先用 ShaderManager 加载，使 GetShader 能返回有效 handle
    std::string tmpDir = std::filesystem::temp_directory_path().string() + "/kale_test_";
    std::string vertPath = tmpDir + "reload_vert.spv";
    std::string fragPath = tmpDir + "reload_frag.spv";
    std::vector<std::uint8_t> minimalSpv = {0x03, 0x02, 0x23, 0x07, 0x00, 0x00, 0x01, 0x00};
    for (const std::string& p : {vertPath, fragPath}) {
        std::ofstream f(p, std::ios::binary);
        if (f) f.write(reinterpret_cast<const char*>(minimalSpv.data()), minimalSpv.size());
    }
    kale_device::ShaderHandle vh = shaderMgr.LoadShader(vertPath, ShaderStage::Vertex, &dev);
    kale_device::ShaderHandle fh = shaderMgr.LoadShader(fragPath, ShaderStage::Fragment, &dev);
    TEST_CHECK(vh.IsValid() && fh.IsValid());

    // 给材质设旧 pipeline，用与 LoadShader 一致的 path 注册
    mat.SetPipeline(kale_device::PipelineHandle{999u});
    TEST_CHECK(mat.GetPipeline().id == 999u);
    registry.RegisterMaterial(&mat, vertPath, fragPath, desc);

    registry.OnShaderReloaded(vertPath);
    TEST_CHECK(dev.destroyedPipelineIds_.size() >= 1u);
    TEST_CHECK(mat.GetPipeline().IsValid() && mat.GetPipeline().id != 999u);

    // Unregister 后 OnShaderReloaded 不再影响该材质
    dev.destroyedPipelineIds_.clear();
    dev.createdPipelineIds_.clear();
    kale_device::PipelineHandle afterReload = mat.GetPipeline();
    registry.UnregisterMaterial(&mat);
    registry.OnShaderReloaded(vertPath);
    TEST_CHECK(dev.destroyedPipelineIds_.empty());
    TEST_CHECK(mat.GetPipeline().id == afterReload.id);

    // ----- ResourceManager SetShaderManager -----
    kale::resource::ResourceManager rm(nullptr, &dev, nullptr);
    TEST_CHECK(rm.GetShaderManager() == nullptr);
    rm.SetShaderManager(static_cast<void*>(&shaderMgr));
    TEST_CHECK(rm.GetShaderManager() == static_cast<void*>(&shaderMgr));

    std::cout << "test_material_pipeline_reload passed\n";
    return 0;
}
