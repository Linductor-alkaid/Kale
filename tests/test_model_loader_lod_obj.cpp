/**
 * @file test_model_loader_lod_obj.cpp
 * @brief phase13-13.11 ModelLoader 可选格式与 LOD 单元测试
 *
 * 覆盖：path#lodN 仅加载第 N 个 glTF mesh；LOD 越界失败；.obj 简易解析加载。
 */

#include <kale_resource/model_loader.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <typeindex>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__       \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

namespace {

class MockModelDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&,
                                           const void*) override {
        return kale_device::BufferHandle{++nextId_};
    }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&,
                                             const void*) override {
        return {};
    }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override {
        return {};
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
    void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override {
        return nullptr;
    }
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

    std::uint32_t AcquireNextImage() override {
        return kale_device::IRenderDevice::kInvalidSwapchainImageIndex;
    }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    void SetExtent(std::uint32_t, std::uint32_t) override {}

    const kale_device::DeviceCapabilities& GetCapabilities() const override {
        return caps_;
    }

private:
    std::string err_;
    std::uint64_t nextId_ = 1;
    kale_device::DeviceCapabilities caps_{};
};

}  // namespace

int main() {
    using namespace kale::resource;
    ModelLoader loader;
    TEST_CHECK(loader.Supports("model.obj"));
    TEST_CHECK(loader.Supports("model.gltf"));
    TEST_CHECK(loader.Supports("model.gltf#lod0"));
    TEST_CHECK(loader.Supports("model.glb#lod1"));

    ResourceManager mgr(nullptr, nullptr, nullptr);
    mgr.SetAssetPath(".");
    mgr.RegisterLoader(std::make_unique<ModelLoader>());

    MockModelDevice dev;
    kale_device::DeviceConfig cfg{};
    TEST_CHECK(dev.Initialize(cfg));

    ResourceLoadContext ctx{};
    ctx.device = &dev;
    ctx.resourceManager = &mgr;

    // 与 test_full_model_loader 相同的 glTF；若该文件已存在（如刚运行过 test_full_model_loader）则直接用，否则创建
    std::string gltfPath = "/tmp/kale_test_full_model_loader.gltf";
    std::ifstream probe(gltfPath);
    if (!probe.good()) {
        // 42 字节需 56 字符 base64；不足则用 = 填充
        const char* minimalGltf = R"({
            "asset": {"version": "2.0"},
            "scene": 0,
            "scenes": [{"nodes": [0]}],
            "nodes": [{"mesh": 0}],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 1}, "indices": 0, "material": 0, "mode": 4}]}],
            "materials": [{"name": "TestMat"}],
            "accessors": [{"bufferView": 0, "componentType": 5123, "count": 3, "type": "SCALAR"}, {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"}],
            "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 6, "target": 34963}, {"buffer": 0, "byteOffset": 6, "byteLength": 36, "target": 34962}],
            "buffers": [{"byteLength": 42, "uri": "data:application/octet-stream;base64,AAABAAIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA===="}]
        })";
        std::ofstream f(gltfPath);
        TEST_CHECK(f);
        f << minimalGltf;
    }

    // 无 #lod：合并（单 mesh 时即 1 个 mesh）
    std::any merged = loader.Load(gltfPath, ctx);
    TEST_CHECK(merged.has_value());
    Mesh* meshMerged = std::any_cast<Mesh*>(merged);
    TEST_CHECK(meshMerged != nullptr);
    TEST_CHECK(meshMerged->subMeshes.size() >= 1u);
    TEST_CHECK(meshMerged->subMeshes[0].indexCount == 3u);

    // #lod0：仅第一个 mesh（单 mesh 文件等价于合并）
    std::any lod0 = loader.Load(gltfPath + "#lod0", ctx);
    TEST_CHECK(lod0.has_value());
    Mesh* meshLod0 = std::any_cast<Mesh*>(lod0);
    TEST_CHECK(meshLod0 != nullptr);
    TEST_CHECK(meshLod0->subMeshes.size() == 1u);
    TEST_CHECK(meshLod0->subMeshes[0].indexCount == 3u);

    // #lod1：仅 1 个 mesh 时越界，应失败
    std::any lod1 = loader.Load(gltfPath + "#lod1", ctx);
    TEST_CHECK(!lod1.has_value());
    TEST_CHECK(!mgr.GetLastError().empty());

    // #lod99：越界，应失败
    std::any lod99 = loader.Load(gltfPath + "#lod99", ctx);
    TEST_CHECK(!lod99.has_value());
    TEST_CHECK(!mgr.GetLastError().empty());

    // 最小 OBJ：3 个顶点、1 个三角形
    std::string objPath = "/tmp/kale_test_model_loader_obj.obj";
    {
        std::ofstream f(objPath);
        TEST_CHECK(f);
        f << "v 0 0 0\nv 1 0 0\nv 0.5 1 0\n";
        f << "f 1 2 3\n";
    }

    std::any objResult = loader.Load(objPath, ctx);
    TEST_CHECK(objResult.has_value());
    Mesh* meshObj = std::any_cast<Mesh*>(objResult);
    TEST_CHECK(meshObj != nullptr);
    TEST_CHECK(meshObj->vertexCount == 3u);
    TEST_CHECK(meshObj->indexCount == 3u);
    TEST_CHECK(meshObj->vertexBuffer.IsValid());
    TEST_CHECK(meshObj->indexBuffer.IsValid());
    TEST_CHECK(meshObj->subMeshes.size() == 1u);
    TEST_CHECK(meshObj->materialPaths.size() == 1u);

    // OBJ 无效路径
    std::any objInvalid = loader.Load("/nonexistent/kale_test_12345.obj", ctx);
    TEST_CHECK(!objInvalid.has_value());

    return 0;
}
