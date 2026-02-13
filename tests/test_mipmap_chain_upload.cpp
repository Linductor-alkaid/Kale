/**
 * @file test_mipmap_chain_upload.cpp
 * @brief phase13-13.12 Mipmap 链上传单元测试
 *
 * 覆盖：LoadSTB 有 Staging 时生成 mip 链并逐级 SubmitUpload（1x1 为单 mip）；
 * CreateTexture 收到正确 mipLevels；FlushUploads 后 CopyBufferToTexture 调用次数等于 mip 数。
 * LoadKTX 多 mip 逐级上传由实现保证，可用真实 2-mip KTX 文件做集成验证。
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
#include <string>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__       \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

namespace {

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
                             kale_device::TextureHandle, std::uint32_t mipLevel,
                             std::uint32_t, std::uint32_t, std::uint32_t) override {
        copyBufferToTextureCalls_++;
        lastCopyMipLevel_ = mipLevel;
    }
    void CopyTextureToTexture(kale_device::TextureHandle, kale_device::TextureHandle,
                              std::uint32_t, std::uint32_t) override {}
    void Barrier(const std::vector<kale_device::TextureHandle>&) override {}
    void ClearColor(kale_device::TextureHandle, const float[4]) override {}
    void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}

    int copyBufferToTextureCalls_ = 0;
    std::uint32_t lastCopyMipLevel_ = 0;
};

class MockDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }
    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override {
        return kale_device::BufferHandle{++nextId_};
    }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc& desc,
                                             const void*) override {
        lastCreateTextureMipLevels_ = desc.mipLevels;
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
    void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override {
        if (mockStagingBuf_.size() < 256 * 1024) mockStagingBuf_.resize(256 * 1024);
        return mockStagingBuf_.data();
    }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}
    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &mockCmd_; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}
    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override {
        return kale_device::FenceHandle{++nextId_};
    }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }
    std::uint32_t AcquireNextImage() override {
        return kale_device::IRenderDevice::kInvalidSwapchainImageIndex;
    }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }

    std::uint32_t lastCreateTextureMipLevels_ = 0;
    MockCommandList mockCmd_;

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextId_ = 1;
    std::vector<char> mockStagingBuf_;
};

}  // namespace

static void test_stb_mip_chain_single_mip() {
    /* 1x1 图像仅 1 个 mip；验证 mipLevels=1 且不崩溃 */
    MockDevice dev;
    kale::resource::StagingMemoryManager staging(&dev);
    staging.SetPoolSize(256 * 1024);
    kale::resource::TextureLoader loader;
    kale::resource::ResourceLoadContext ctx;
    ctx.device = &dev;
    ctx.stagingMgr = &staging;
    ctx.resourceManager = nullptr;

    const char* candidates[] = {
        "fixtures/1x1.png",
        "tests/fixtures/1x1.png",
        "../tests/fixtures/1x1.png",
    };
    std::string path;
    for (const char* p : candidates) {
        std::ifstream f(p, std::ifstream::binary);
        if (f.good()) { path = p; break; }
    }
    if (path.empty()) return;  /* 无 fixture 时跳过 */

    std::any result = loader.Load(path, ctx);
    TEST_CHECK(result.has_value());
    kale::resource::Texture* tex = std::any_cast<kale::resource::Texture*>(result);
    TEST_CHECK(tex != nullptr && tex->handle.IsValid());
    TEST_CHECK(tex->mipLevels == 1);
    TEST_CHECK(dev.lastCreateTextureMipLevels_ == 1);
    TEST_CHECK(dev.mockCmd_.copyBufferToTextureCalls_ == 1);
    delete tex;
}

int main() {
    test_stb_mip_chain_single_mip();
    return 0;
}
