/**
 * @file test_placeholders.cpp
 * @brief ResourceManager 占位符系统单元测试（phase4-4.5）
 *
 * 覆盖：无 device 时 CreatePlaceholders 不创建、GetPlaceholder* 返回 nullptr；
 * 有 mock device 时 CreatePlaceholders 创建 Mesh/Texture/Material，GetPlaceholder* 返回非空。
 */

#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>

#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

namespace {

/** Mock 设备：仅用于 CreatePlaceholders 的 CreateBuffer/CreateTexture 返回有效句柄 */
class MockPlaceholderDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&,
                                          const void*) override {
        nextId_++;
        return kale_device::BufferHandle{nextId_};
    }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&,
                                             const void*) override {
        nextId_++;
        return kale_device::TextureHandle{nextId_};
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

    const kale_device::DeviceCapabilities& GetCapabilities() const override {
        return caps_;
    }

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextId_ = 1;
};

}  // namespace

static void test_placeholders_no_device_returns_null() {
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    rm.CreatePlaceholders();

    TEST_CHECK(rm.GetPlaceholderMesh() == nullptr);
    TEST_CHECK(rm.GetPlaceholderTexture() == nullptr);
    TEST_CHECK(rm.GetPlaceholderMaterial() == nullptr);
}

static void test_placeholders_with_mock_device_creates_all() {
    MockPlaceholderDevice dev;
    kale::resource::ResourceManager rm(nullptr, &dev, nullptr);
    rm.CreatePlaceholders();

    kale::resource::Mesh* mesh = rm.GetPlaceholderMesh();
    TEST_CHECK(mesh != nullptr);
    TEST_CHECK(mesh->vertexBuffer.IsValid());
    TEST_CHECK(mesh->indexBuffer.IsValid());
    TEST_CHECK(mesh->indexCount == 3u);
    TEST_CHECK(mesh->vertexCount == 3u);
    TEST_CHECK(mesh->subMeshes.size() == 1u);
    TEST_CHECK(mesh->subMeshes[0].indexCount == 3u);

    kale::resource::Texture* tex = rm.GetPlaceholderTexture();
    TEST_CHECK(tex != nullptr);
    TEST_CHECK(tex->handle.IsValid());
    TEST_CHECK(tex->width == 1u && tex->height == 1u);

    kale::resource::Material* mat = rm.GetPlaceholderMaterial();
    TEST_CHECK(mat != nullptr);
}

int main() {
    test_placeholders_no_device_returns_null();
    test_placeholders_with_mock_device_creates_all();
    std::cout << "test_placeholders: all passed\n";
    return 0;
}
