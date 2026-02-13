/**
 * @file test_release_frame_resources.cpp
 * @brief phase7-7.10 ReleaseFrameResources 单元测试
 *
 * 覆盖：RenderGraph::ReleaseFrameResources() 遍历 submittedDraws_ 调用 renderable->ReleaseFrameResources()；
 * 经 Renderable 链调用到 Material::ReleaseAllInstanceDescriptorSets()，池化回收后再次 Acquire 复用；
 * 空 submittedDraws 不崩溃；多 renderable 均被回收。
 */

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/material.hpp>
#include <kale_scene/renderable.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_resource/resource_types.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                               \
    } while (0)

namespace {

/** Mock 设备：仅用于 Material::AcquireInstanceDescriptorSet 的 CreateDescriptorSet/CreateBuffer/WriteDescriptorSetBuffer */
class MockDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc& desc, const void*) override {
        if (kale_device::HasBufferUsage(desc.usage, kale_device::BufferUsage::Uniform)) {
            nextBufferId_++;
            kale_device::BufferHandle h;
            h.id = nextBufferId_;
            return h;
        }
        return {};
    }
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
                                   kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t,
                                 kale_device::BufferHandle, std::size_t, std::size_t) override {}

    void DestroyBuffer(kale_device::BufferHandle) override {}
    void DestroyTexture(kale_device::TextureHandle) override {}
    void DestroyShader(kale_device::ShaderHandle) override {}
    void DestroyPipeline(kale_device::PipelineHandle) override {}
    void DestroyDescriptorSet(kale_device::DescriptorSetHandle) override {}

    kale_device::DescriptorSetHandle AcquireInstanceDescriptorSet(const void*, std::size_t size) override {
        if (size > kale_device::kInstanceDescriptorDataSize) size = kale_device::kInstanceDescriptorDataSize;
        kale_device::DescriptorSetHandle h;
        if (!instancePoolFree_.empty()) {
            h.id = instancePoolFree_.back();
            instancePoolFree_.pop_back();
        } else {
            nextSetId_++;
            h.id = nextSetId_;
        }
        return h;
    }
    void ReleaseInstanceDescriptorSet(kale_device::DescriptorSetHandle handle) override {
        if (handle.IsValid()) instancePoolFree_.push_back(handle.id);
    }

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

    std::uint64_t createSetCount() const { return nextSetId_; }

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextSetId_ = 0;
    std::uint64_t nextBufferId_ = 0;
    std::vector<std::uint64_t> instancePoolFree_;
};

/** 测试用 Renderable：持有 pipeline::Material*，ReleaseFrameResources 时调用 material->ReleaseFrameResources()。 */
class RenderableWithMaterial : public kale::scene::Renderable {
public:
    explicit RenderableWithMaterial(kale::pipeline::Material* material) : material_(material) {}

    kale::resource::BoundingBox GetBounds() const override {
        kale::resource::BoundingBox b;
        b.min = glm::vec3(0.f);
        b.max = glm::vec3(1.f);
        return b;
    }
    const kale::resource::Material* GetMaterial() const override { return material_; }
    void Draw(kale_device::CommandList&, const glm::mat4&, kale_device::IRenderDevice*) override {}

    void ReleaseFrameResources() override {
        if (material_)
            material_->ReleaseFrameResources();
    }

private:
    kale::pipeline::Material* material_ = nullptr;
};

static void test_release_frame_resources_recycles_pool() {
    MockDevice dev;
    dev.Initialize({});

    kale::pipeline::Material mat;
    float data[16] = {};
    mat.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    std::uint64_t n = dev.createSetCount();
    TEST_CHECK(n >= 1u);

    RenderableWithMaterial r(&mat);
    kale::pipeline::RenderGraph rg;
    rg.SubmitRenderable(&r, glm::mat4(1.f), kale::scene::PassFlags::All);
    rg.ReleaseFrameResources();

    kale_device::DescriptorSetHandle h2 = mat.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    TEST_CHECK(h2.IsValid());
    TEST_CHECK(dev.createSetCount() == n);

    dev.Shutdown();
}

static void test_empty_submitted_draws_no_crash() {
    kale::pipeline::RenderGraph rg;
    rg.ReleaseFrameResources();
}

static void test_multiple_renderables_all_released() {
    MockDevice dev;
    dev.Initialize({});

    kale::pipeline::Material mat1;
    kale::pipeline::Material mat2;
    float data[16] = {};
    mat1.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    mat2.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    std::uint64_t n = dev.createSetCount();
    TEST_CHECK(n >= 2u);

    RenderableWithMaterial r1(&mat1);
    RenderableWithMaterial r2(&mat2);
    kale::pipeline::RenderGraph rg;
    rg.SubmitRenderable(&r1, glm::mat4(1.f));
    rg.SubmitRenderable(&r2, glm::mat4(1.f));
    rg.ReleaseFrameResources();

    mat1.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    mat2.AcquireInstanceDescriptorSet(&dev, data, sizeof(data));
    TEST_CHECK(dev.createSetCount() == n);

    dev.Shutdown();
}

}  // namespace

int main() {
    test_release_frame_resources_recycles_pool();
    test_empty_submitted_draws_no_crash();
    test_multiple_renderables_all_released();
    return 0;
}
