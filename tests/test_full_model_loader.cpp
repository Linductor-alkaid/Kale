/**
 * @file test_full_model_loader.cpp
 * @brief phase8-8.1 完整 ModelLoader 单元测试
 *
 * 覆盖：glTF 材质引用、materialPaths 与 SubMesh materialIndex 映射；
 * 有材质时 materialPaths 为 baseDir/materials/name.json，无材质时 materialPaths 为单元素 ""。
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

/** Mock 设备：CreateBuffer 返回有效句柄，供 ModelLoader 创建顶点/索引缓冲 */
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
    TEST_CHECK(loader.Supports("model.gltf"));
    TEST_CHECK(loader.Supports("model.glb"));
    TEST_CHECK(!loader.Supports("model.obj"));
    TEST_CHECK(loader.GetResourceType() == typeid(Mesh));

    // 最小 glTF：1 个三角形、1 个材质 "TestMat"，buffer 内嵌 base64
    // 索引：3 x uint16 = 6 字节；顶点：3 x vec3 float = 36 字节；共 42 字节
    const char* minimalGltf = R"({
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{
            "primitives": [{
                "attributes": {"POSITION": 1},
                "indices": 0,
                "material": 0,
                "mode": 4
            }]
        }],
        "materials": [{"name": "TestMat"}],
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

    std::string gltfPath = "/tmp/kale_test_full_model_loader.gltf";
    {
        std::ofstream f(gltfPath);
        TEST_CHECK(f);
        f << minimalGltf;
    }

    ResourceManager mgr(nullptr, nullptr, nullptr);
    mgr.SetAssetPath(".");
    mgr.RegisterLoader(std::make_unique<ModelLoader>());

    MockModelDevice dev;
    kale_device::DeviceConfig cfg{};
    TEST_CHECK(dev.Initialize(cfg));

    ResourceLoadContext ctx{};
    ctx.device = &dev;
    ctx.resourceManager = &mgr;

    std::any result = loader.Load(gltfPath, ctx);
    TEST_CHECK(result.has_value());
    Mesh* mesh = std::any_cast<Mesh*>(result);
    TEST_CHECK(mesh != nullptr);

    // 完整 ModelLoader：materialPaths 与 SubMesh 材质映射
    TEST_CHECK(!mesh->materialPaths.empty());
    TEST_CHECK(mesh->materialPaths.size() >= 1u);
    // baseDir 为 "."，材质名为 "TestMat" -> "./materials/TestMat.json"
    TEST_CHECK(mesh->materialPaths[0].find("TestMat") != std::string::npos);
    TEST_CHECK(mesh->materialPaths[0].find(".json") != std::string::npos);

    TEST_CHECK(!mesh->subMeshes.empty());
    TEST_CHECK(mesh->subMeshes[0].materialIndex == 0u);
    TEST_CHECK(mesh->subMeshes[0].indexCount == 3u);

    // 无材质 glTF：materialPaths 应为单元素（空或默认）
    const char* noMaterialGltf = R"({
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
    std::string noMatPath = "/tmp/kale_test_full_model_loader_nomat.gltf";
    {
        std::ofstream f(noMatPath);
        TEST_CHECK(f);
        f << noMaterialGltf;
    }
    std::any result3 = loader.Load(noMatPath, ctx);
    TEST_CHECK(result3.has_value());
    Mesh* mesh3 = std::any_cast<Mesh*>(result3);
    TEST_CHECK(mesh3 != nullptr);
    TEST_CHECK(mesh3->materialPaths.size() == 1u);
    TEST_CHECK(mesh3->subMeshes[0].materialIndex == 0u);

    return 0;
}
