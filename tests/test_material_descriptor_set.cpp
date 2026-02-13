/**
 * @file test_material_descriptor_set.cpp
 * @brief phase7-7.8 材质级 DescriptorSet 单元测试
 *
 * 覆盖：GetMaterialDescriptorSet 默认无效；EnsureMaterialDescriptorSet(nullptr) 无操作；
 * 无纹理时 Ensure 不创建 set；Mock 设备下有一纹理时 Ensure 后返回有效 set；同一材质共享同一 handle。
 */

#include <kale_pipeline/material.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <iostream>
#include <stdexcept>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                               \
    } while (0)

namespace {

/** Mock 设备：CreateDescriptorSet 返回有效句柄，WriteDescriptorSetTexture / DestroyDescriptorSet 可调用 */
class MockMaterialDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override { return {}; }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override { return {}; }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }

    kale_device::DescriptorSetHandle CreateDescriptorSet(
        const kale_device::DescriptorSetLayoutDesc& layout) override {
        if (layout.bindings.empty()) return {};
        nextSetId_++;
        kale_device::DescriptorSetHandle h;
        h.id = nextSetId_;
        return h;
    }

    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t,
                                   kale_device::TextureHandle) override {
        writeTextureCount_++;
    }

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

    std::uint64_t writeTextureCount() const { return writeTextureCount_; }

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextSetId_ = 0;
    std::uint64_t writeTextureCount_ = 0;
};

static void test_get_material_descriptor_set_default_invalid() {
    kale::pipeline::Material mat;
    TEST_CHECK(!mat.GetMaterialDescriptorSet().IsValid());
}

static void test_ensure_with_null_device_no_op() {
    kale::pipeline::Material mat;
    kale::resource::Texture tex;
    tex.handle.id = 1u;
    mat.SetTexture("albedo", &tex);
    mat.EnsureMaterialDescriptorSet(nullptr);
    TEST_CHECK(!mat.GetMaterialDescriptorSet().IsValid());
}

static void test_ensure_with_no_textures_leaves_invalid() {
    MockMaterialDevice dev;
    dev.Initialize({});
    kale::pipeline::Material mat;
    mat.EnsureMaterialDescriptorSet(&dev);
    TEST_CHECK(!mat.GetMaterialDescriptorSet().IsValid());
    dev.Shutdown();
}

static void test_ensure_with_one_texture_returns_valid() {
    MockMaterialDevice dev;
    dev.Initialize({});
    kale::pipeline::Material mat;
    kale::resource::Texture tex;
    tex.handle.id = 100u;
    mat.SetTexture("albedo", &tex);
    mat.EnsureMaterialDescriptorSet(&dev);
    TEST_CHECK(mat.GetMaterialDescriptorSet().IsValid());
    TEST_CHECK(dev.writeTextureCount() == 1u);
    dev.Shutdown();
}

static void test_same_material_same_descriptor_set_handle() {
    MockMaterialDevice dev;
    dev.Initialize({});
    kale::pipeline::Material mat;
    kale::resource::Texture tex;
    tex.handle.id = 1u;
    mat.SetTexture("diffuse", &tex);
    mat.EnsureMaterialDescriptorSet(&dev);
    kale_device::DescriptorSetHandle h1 = mat.GetMaterialDescriptorSet();
    TEST_CHECK(h1.IsValid());
    kale_device::DescriptorSetHandle h2 = mat.GetMaterialDescriptorSet();
    TEST_CHECK(h2.id == h1.id);
    dev.Shutdown();
}

static void test_multiple_textures_writes_all_bindings() {
    MockMaterialDevice dev;
    dev.Initialize({});
    kale::pipeline::Material mat;
    kale::resource::Texture t1, t2;
    t1.handle.id = 1u;
    t2.handle.id = 2u;
    mat.SetTexture("albedo", &t1);
    mat.SetTexture("normal", &t2);
    mat.EnsureMaterialDescriptorSet(&dev);
    TEST_CHECK(mat.GetMaterialDescriptorSet().IsValid());
    TEST_CHECK(dev.writeTextureCount() == 2u);
    dev.Shutdown();
}

}  // namespace

int main() {
    test_get_material_descriptor_set_default_invalid();
    test_ensure_with_null_device_no_op();
    test_ensure_with_no_textures_leaves_invalid();
    test_ensure_with_one_texture_returns_valid();
    test_same_material_same_descriptor_set_handle();
    test_multiple_textures_writes_all_bindings();
    return 0;
}
