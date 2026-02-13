/**
 * @file test_model_loader_staging.cpp
 * @brief phase6-6.5 ModelLoader 集成 Staging 单元测试
 *
 * 覆盖：ctx.stagingMgr 非空时走 Staging 路径（CreateBuffer(desc,nullptr) x2 +
 * Allocate + memcpy + SubmitUpload + FlushUploads + WaitForFence + Free）；
 * 无 stagingMgr 时回退 CreateBuffer(desc, data)；无效路径返回空。
 */

#include <kale_resource/model_loader.hpp>
#include <kale_resource/staging_memory_manager.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>

#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__         \
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
                            kale_device::BufferHandle, std::size_t, std::size_t) override {
        copyBufferToBufferCalls_++;
    }
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

    int copyBufferToBufferCalls_ = 0;
};

class MockStagingDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&,
                                           const void* data) override {
        if (data == nullptr)
            createBufferNullCalls_++;
        nextId_++;
        return kale_device::BufferHandle{nextId_};
    }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&,
                                             const void*) override {
        return {};
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
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }

    std::uint32_t AcquireNextImage() override {
        return kale_device::IRenderDevice::kInvalidSwapchainImageIndex;
    }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    void SetExtent(std::uint32_t, std::uint32_t) override {}

    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }

    int createBufferNullCalls_ = 0;
    std::uint64_t lastSubmittedFenceId_ = 0;
    MockCommandList mockCmd_;

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextId_ = 1;
    char mappedArea_[256 * 1024];
};

static const char* minimalGltf() {
    return R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{
            "primitives": [{
                "attributes": {"POSITION": 1},
                "indices": 0,
                "mode": 4
            }]
        }],
        "accessors": [
            {"bufferView": 0, "componentType": 5123, "count": 3, "type": "SCALAR"},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"}
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 6, "target": 34963},
            {"buffer": 0, "byteOffset": 6, "byteLength": 36, "target": 34962}
        ],
        "buffers": [{
            "byteLength": 42,
            "uri": "data:application/octet-stream;base64,AAABAAIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
        }]
    })";
}

static std::string write_minimal_gltf(const std::string& path) {
    std::ofstream f(path);
    TEST_CHECK(f);
    f << minimalGltf();
    return path;
}

}  // namespace

static void test_model_loader_with_staging_succeeds() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager staging(&dev);
    staging.SetPoolSize(64 * 1024);

    kale::resource::ModelLoader loader;
    kale::resource::ResourceManager mgr(nullptr, nullptr, nullptr);
    mgr.SetAssetPath(".");
    kale::resource::ResourceLoadContext ctx;
    ctx.device = &dev;
    ctx.stagingMgr = &staging;
    ctx.resourceManager = &mgr;

    std::string gltfPath = "/tmp/kale_test_model_loader_staging.gltf";
    write_minimal_gltf(gltfPath);

    std::any result = loader.Load(gltfPath, ctx);
    TEST_CHECK(result.has_value());
    kale::resource::Mesh* mesh = std::any_cast<kale::resource::Mesh*>(result);
    TEST_CHECK(mesh != nullptr);
    TEST_CHECK(mesh->vertexBuffer.IsValid());
    TEST_CHECK(mesh->indexBuffer.IsValid());
    TEST_CHECK(mesh->indexCount == 3u);
    TEST_CHECK(mesh->vertexCount == 3u);
    /* Staging 路径会先 CreateBuffer(desc, nullptr) 再 Allocate；至少有一次 CreateBuffer(nullptr)（池或 mesh） */
    TEST_CHECK(dev.createBufferNullCalls_ >= 1);
    /* FlushUploads 会 Submit，应有 fence 被提交 */
    TEST_CHECK(dev.lastSubmittedFenceId_ != 0);
    /* SubmitUpload 到 buffer 经 FlushUploads 触发 CopyBufferToBuffer */
    TEST_CHECK(dev.mockCmd_.copyBufferToBufferCalls_ >= 2);
    delete mesh;
}

static void test_model_loader_without_staging_fallback() {
    MockStagingDevice dev;
    kale::resource::ModelLoader loader;
    kale::resource::ResourceManager mgr(nullptr, nullptr, nullptr);
    mgr.SetAssetPath(".");
    kale::resource::ResourceLoadContext ctx;
    ctx.device = &dev;
    ctx.stagingMgr = nullptr;
    ctx.resourceManager = &mgr;

    std::string gltfPath = "/tmp/kale_test_model_loader_staging_fallback.gltf";
    write_minimal_gltf(gltfPath);

    std::any result = loader.Load(gltfPath, ctx);
    TEST_CHECK(result.has_value());
    kale::resource::Mesh* mesh = std::any_cast<kale::resource::Mesh*>(result);
    TEST_CHECK(mesh != nullptr && mesh->vertexBuffer.IsValid());
    /* 回退路径：CreateBuffer(desc, data) 传入非空 data，故 createBufferNullCalls_ 为 0 */
    TEST_CHECK(dev.createBufferNullCalls_ == 0);
    delete mesh;
}

static void test_model_loader_invalid_path_returns_empty() {
    MockStagingDevice dev;
    kale::resource::StagingMemoryManager staging(&dev);
    kale::resource::ModelLoader loader;
    kale::resource::ResourceManager mgr(nullptr, nullptr, nullptr);
    mgr.SetAssetPath(".");
    kale::resource::ResourceLoadContext ctx;
    ctx.device = &dev;
    ctx.stagingMgr = &staging;
    ctx.resourceManager = &mgr;

    std::any result = loader.Load("/nonexistent/model.gltf", ctx);
    TEST_CHECK(!result.has_value());
}

int main() {
    test_model_loader_with_staging_succeeds();
    test_model_loader_without_staging_fallback();
    test_model_loader_invalid_path_returns_empty();
    return 0;
}
