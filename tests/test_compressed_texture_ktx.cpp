/**
 * @file test_compressed_texture_ktx.cpp
 * @brief phase12-12.5 压缩纹理支持（KTX）单元测试
 *
 * 覆盖：TextureLoader::Supports(".ktx")；LoadKTX 无效路径/错误 magic 返回空；
 * 最小合法 KTX1（4x4 RGBA8）经 Staging 加载后句柄有效、宽高与 Format 正确。
 */

#include <kale_resource/staging_memory_manager.hpp>
#include <kale_resource/texture_loader.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                     \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

namespace {

constexpr std::uint8_t KTX1_IDENTIFIER[12] = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
};
constexpr std::uint32_t GL_RGBA8 = 0x8058u;
constexpr std::uint32_t GL_RGBA = 0x1908u;
constexpr std::uint32_t GL_UNSIGNED_BYTE = 0x1401u;

class MockCommandList : public kale_device::CommandList {
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
    void ClearColor(kale_device::TextureHandle, const float[4]) override {}
    void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
};

class MockDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }
    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override {
        return kale_device::BufferHandle{++nextId_};
    }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override {
        return kale_device::TextureHandle{++nextId_};
    }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }
    kale_device::DescriptorSetHandle CreateDescriptorSet(
        const kale_device::DescriptorSetLayoutDesc&) override { return {}; }
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
    void* MapBuffer(kale_device::BufferHandle, std::size_t offset, std::size_t size) override {
        (void)offset;
        if (size > mockStagingBuf_.size()) mockStagingBuf_.resize(size);
        return mockStagingBuf_.empty() ? nullptr : mockStagingBuf_.data();
    }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}
    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &cmd_; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}
    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override { return kale_device::FenceHandle{1}; }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }
    std::uint32_t AcquireNextImage() override { return 0; }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }
    kale_device::DescriptorSetHandle AcquireInstanceDescriptorSet(const void*, std::size_t) override { return {}; }
    void ReleaseInstanceDescriptorSet(kale_device::DescriptorSetHandle) override {}
    void SetExtent(std::uint32_t, std::uint32_t) override {}

    std::string err_;
    kale_device::DeviceCapabilities caps_{};
    std::uint64_t nextId_ = 0;
    std::vector<std::uint8_t> mockStagingBuf_;
    MockCommandList cmd_;
};

void WriteU32(std::ostream& out, std::uint32_t v) {
    out.write(reinterpret_cast<const char*>(&v), 4);
}

/** 写入最小合法 KTX1：4x4 RGBA8，单 mip，无 key/value */
bool WriteMinimalKTX1(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(KTX1_IDENTIFIER), 12);
    WriteU32(f, 0x01020304u);  /* little endian */
    WriteU32(f, GL_UNSIGNED_BYTE);
    WriteU32(f, GL_RGBA);
    WriteU32(f, GL_RGBA8);
    WriteU32(f, GL_RGBA);
    WriteU32(f, 4);   /* pixelWidth */
    WriteU32(f, 4);   /* pixelHeight */
    WriteU32(f, 0);   /* pixelDepth */
    WriteU32(f, 0);   /* numberOfArrayElements */
    WriteU32(f, 1);   /* numberOfFaces */
    WriteU32(f, 1);   /* numberOfMipmapLevels */
    WriteU32(f, 0);   /* bytesOfKeyValueData */
    std::uint32_t imageSize = 4 * 4 * 4;  /* 64 */
    WriteU32(f, imageSize);
    for (std::uint32_t i = 0; i < imageSize; ++i)
        f.put(static_cast<char>(i & 0xFF));
    return f.good();
}

}  // namespace

int main() {
    kale::resource::TextureLoader loader;

    /* Supports .ktx 与 .png */
    TEST_CHECK(loader.Supports("a.ktx"));
    TEST_CHECK(loader.Supports("b.KTX"));
    TEST_CHECK(loader.Supports("x.png"));
    TEST_CHECK(!loader.Supports("x.unk"));

    /* 无效路径返回空 */
    kale::resource::ResourceLoadContext ctxNull{};
    ctxNull.device = nullptr;
    auto anyNull = loader.Load("/nonexistent/path.ktx", ctxNull);
    TEST_CHECK(!anyNull.has_value());

    MockDevice dev;
    kale::resource::StagingMemoryManager staging(&dev);
    kale::resource::ResourceManager* mgr = nullptr;
    kale::resource::ResourceLoadContext ctx;
    ctx.device = &dev;
    ctx.stagingMgr = &staging;
    ctx.resourceManager = mgr;

    auto anyBad = loader.Load("/nonexistent.ktx", ctx);
    TEST_CHECK(!anyBad.has_value());

    /* 错误 magic 返回空：写一个只含 12 字节错误标识的文件 */
    std::string badPath = "test_ktx_bad_magic.ktx";
    {
        std::ofstream f(badPath, std::ios::binary);
        for (int i = 0; i < 12; ++i) f.put(0);
    }
    auto anyMagic = loader.Load(badPath, ctx);
    TEST_CHECK(!anyMagic.has_value());

    /* 最小合法 KTX1 加载 */
    std::string ktxPath = "test_minimal.ktx";
    TEST_CHECK(WriteMinimalKTX1(ktxPath));
    std::any anyTex = loader.Load(ktxPath, ctx);
    TEST_CHECK(anyTex.has_value());
    kale::resource::Texture* raw = std::any_cast<kale::resource::Texture*>(anyTex);
    TEST_CHECK(raw != nullptr);
    std::unique_ptr<kale::resource::Texture> owned(raw);
    TEST_CHECK(owned->handle.IsValid());
    TEST_CHECK(owned->width == 4 && owned->height == 4);
    TEST_CHECK(owned->format == kale_device::Format::RGBA8_UNORM);
    TEST_CHECK(owned->mipLevels >= 1);

    /* 清理临时文件 */
    std::remove(badPath.c_str());
    std::remove(ktxPath.c_str());

    return 0;
}
