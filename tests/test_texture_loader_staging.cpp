/**
 * @file test_texture_loader_staging.cpp
 * @brief phase6-6.4 TextureLoader 集成 Staging 单元测试
 *
 * 覆盖：ctx.stagingMgr 非空时走 Staging 路径（CreateTexture(desc,nullptr) + Allocate +
 * SubmitUpload + FlushUploads + WaitForFence + Free）；加载成功返回有效 Texture；
 * 无 stagingMgr 时回退 CreateTexture(desc, data)。
 * 使用 tests/fixtures/1x1.png 作为测试图片（CTest WORKING_DIRECTORY=tests）。
 */

#include <kale_resource/staging_memory_manager.hpp>
#include <kale_resource/texture_loader.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>

#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
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
                             std::uint32_t, std::uint32_t, std::uint32_t) override {
        copyTextureCalls_++;
    }
    void CopyTextureToTexture(kale_device::TextureHandle, kale_device::TextureHandle,
                              std::uint32_t, std::uint32_t) override {}
    void Barrier(const std::vector<kale_device::TextureHandle>&) override {}
    void ClearColor(kale_device::TextureHandle, const float[4]) override {}
    void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}

    int copyTextureCalls_ = 0;
};

class MockStagingDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&,
                                           const void*) override {
        return kale_device::BufferHandle{++nextId_};
    }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc& desc,
                                             const void* data) override {
        /* Staging 路径传入 data=nullptr；回退路径传入像素数据 */
        if (data == nullptr)
            createTextureNullCalls_++;
        nextId_++;
        return kale_device::TextureHandle{nextId_};
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
        return static_cast<void*>(mappedArea_);
    }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}

    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &mockCmd_; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle fence) override {
        if (fence.IsValid())
            lastSubmittedFenceId_ = fence.id;
    }

    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override {
        nextId_++;
        return kale_device::FenceHandle{nextId_};
    }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle h) const override {
        return signaledFences_.count(h.id) != 0;
    }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }

    std::uint32_t AcquireNextImage() override {
        return kale_device::IRenderDevice::kInvalidSwapchainImageIndex;
    }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }

    int createTextureNullCalls_ = 0;
    std::uint64_t lastSubmittedFenceId_ = 0;
    MockCommandList mockCmd_;
    void SignalFence(std::uint64_t id) { signaledFences_.insert(id); }

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextId_ = 1;
    std::set<std::uint64_t> signaledFences_;
    char mappedArea_[256 * 1024];
};

/** 解析 1x1.png fixture 路径：ctest 时 cwd=tests，直接运行时可从 build 或 build/tests */
static std::string get_fixture_1x1_png_path() {
    const char* candidates[] = {
        "fixtures/1x1.png",           /* ctest WORKING_DIRECTORY=tests */
        "tests/fixtures/1x1.png",     /* run from build */
        "../tests/fixtures/1x1.png",   /* run from build/tests */
    };
    for (const char* p : candidates) {
        std::ifstream f(p, std::ifstream::binary);
        if (f.good()) return p;
    }
    return "";
}

}  // namespace

static void test_texture_loader_with_staging_succeeds() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager staging(&dev);
    staging.SetPoolSize(64 * 1024);

    kale::resource::TextureLoader loader;
    kale::resource::ResourceLoadContext ctx;
    ctx.device = &dev;
    ctx.stagingMgr = &staging;
    ctx.resourceManager = nullptr;

    std::string path = get_fixture_1x1_png_path();
    TEST_CHECK(!path.empty() && "fixture fixtures/1x1.png not found");
    std::any result = loader.Load(path, ctx);
    TEST_CHECK(result.has_value());
    kale::resource::Texture* tex = std::any_cast<kale::resource::Texture*>(result);
    TEST_CHECK(tex != nullptr);
    TEST_CHECK(tex->handle.IsValid());
    TEST_CHECK(tex->width == 1 && tex->height == 1);
    /* Staging 路径应调用 CreateTexture(desc, nullptr) */
    TEST_CHECK(dev.createTextureNullCalls_ == 1);
    /* FlushUploads 会 Submit，应有 fence 被提交 */
    TEST_CHECK(dev.lastSubmittedFenceId_ != 0);
    delete tex;
}

static void test_texture_loader_without_staging_fallback() {
    std::string path = get_fixture_1x1_png_path();
    TEST_CHECK(!path.empty());
    MockStagingDevice dev;
    kale::resource::TextureLoader loader;
    kale::resource::ResourceLoadContext ctx;
    ctx.device = &dev;
    ctx.stagingMgr = nullptr;
    ctx.resourceManager = nullptr;

    std::any result = loader.Load(path, ctx);
    TEST_CHECK(result.has_value());
    kale::resource::Texture* tex = std::any_cast<kale::resource::Texture*>(result);
    TEST_CHECK(tex != nullptr && tex->handle.IsValid());
    /* 回退路径：CreateTexture(desc, data) 传入非空 data，故 createTextureNullCalls_ 仍为 0 */
    TEST_CHECK(dev.createTextureNullCalls_ == 0);
    delete tex;
}

static void test_texture_loader_invalid_path_returns_empty() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager staging(&dev);
    kale::resource::TextureLoader loader;
    kale::resource::ResourceLoadContext ctx;
    ctx.device = &dev;
    ctx.stagingMgr = &staging;
    ctx.resourceManager = nullptr;

    std::any result = loader.Load("/nonexistent/texture.png", ctx);
    TEST_CHECK(!result.has_value());
}

int main() {
    test_texture_loader_with_staging_succeeds();
    test_texture_loader_without_staging_fallback();
    test_texture_loader_invalid_path_returns_empty();
    return 0;
}
