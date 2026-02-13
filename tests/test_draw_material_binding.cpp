/**
 * @file test_draw_material_binding.cpp
 * @brief phase7-7.12 Renderable::Draw 与 Material 绑定单元测试
 *
 * 覆盖：Draw 时 BindPipeline、BindDescriptorSet(0 材质级)、BindDescriptorSet(1 实例级)；
 * Material::BindForDraw 由 pipeline::Material 实现；device 为 null 时不绑定实例级 set。
 */

#include <kale_pipeline/material.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_scene/renderable.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <iostream>
#include <glm/glm.hpp>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                               \
    } while (0)

namespace {

/** Mock CommandList：记录 BindPipeline、BindDescriptorSet 调用次数 */
class MockCommandList : public kale_device::CommandList {
public:
    void BeginRenderPass(const std::vector<kale_device::TextureHandle>&,
                         kale_device::TextureHandle) override {}
    void EndRenderPass() override {}
    void BindPipeline(kale_device::PipelineHandle) override { bindPipelineCount_++; }
    void BindDescriptorSet(std::uint32_t set, kale_device::DescriptorSetHandle) override {
        if (set < 2) bindDescriptorSetCount_[set]++;
    }
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
    void ClearColor(kale_device::TextureHandle, const float*) override {}
    void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}

    int bindPipelineCount_ = 0;
    int bindDescriptorSetCount_[2] = {0, 0};
};

/** Mock 设备：CreateDescriptorSet/CreateBuffer 返回有效句柄，供 Material 实例级 set 使用 */
class MockDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override {
        kale_device::BufferHandle h;
        h.id = ++nextId_;
        return h;
    }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override { return {}; }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }
    kale_device::DescriptorSetHandle CreateDescriptorSet(
        const kale_device::DescriptorSetLayoutDesc& layout) override {
        if (layout.bindings.empty()) return {};
        kale_device::DescriptorSetHandle h;
        h.id = ++nextId_;
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
    std::uint32_t AcquireNextImage() override { return kale_device::IRenderDevice::kInvalidSwapchainImageIndex; }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }

    std::string err_;
    kale_device::DeviceCapabilities caps_{};
    std::uint64_t nextId_ = 0;
};

/** 测试用 Renderable：Draw 时调用 material->BindForDraw，然后不录制 draw call（仅测绑定） */
class RenderableWithMaterialBind : public kale::scene::Renderable {
public:
    explicit RenderableWithMaterialBind(kale::resource::Material* material) : material_(material) {}

    kale::resource::BoundingBox GetBounds() const override {
        kale::resource::BoundingBox b;
        b.min = glm::vec3(0.f);
        b.max = glm::vec3(1.f);
        return b;
    }
    const kale::resource::Material* GetMaterial() const override { return material_; }
    void Draw(kale_device::CommandList& cmd, const glm::mat4& worldTransform,
              kale_device::IRenderDevice* device) override {
        if (material_)
            material_->BindForDraw(cmd, device, &worldTransform, sizeof(glm::mat4));
    }

private:
    kale::resource::Material* material_ = nullptr;
};

static void test_material_bind_for_draw_with_device() {
    MockDevice dev;
    dev.Initialize({});

    kale::pipeline::Material mat;
    kale_device::PipelineHandle ph;
    ph.id = 1;
    mat.SetPipeline(ph);

    kale::resource::Texture tex;
    tex.handle.id = 1;
    mat.SetTexture("albedo", &tex);
    mat.EnsureMaterialDescriptorSet(&dev);
    TEST_CHECK(mat.GetMaterialDescriptorSet().IsValid());

    RenderableWithMaterialBind renderable(&mat);
    MockCommandList cmd;

    glm::mat4 identity(1.f);
    renderable.Draw(cmd, identity, &dev);

    TEST_CHECK(cmd.bindPipelineCount_ == 1);
    TEST_CHECK(cmd.bindDescriptorSetCount_[0] == 1);
    TEST_CHECK(cmd.bindDescriptorSetCount_[1] == 1);
}

static void test_material_bind_for_draw_without_device() {
    MockDevice dev;
    dev.Initialize({});

    kale::pipeline::Material mat;
    kale_device::PipelineHandle ph;
    ph.id = 1;
    mat.SetPipeline(ph);
    kale::resource::Texture tex;
    tex.handle.id = 1;
    mat.SetTexture("albedo", &tex);
    mat.EnsureMaterialDescriptorSet(&dev);

    RenderableWithMaterialBind renderable(&mat);
    MockCommandList cmd;

    glm::mat4 identity(1.f);
    renderable.Draw(cmd, identity, nullptr);

    TEST_CHECK(cmd.bindPipelineCount_ == 1);
    TEST_CHECK(cmd.bindDescriptorSetCount_[0] == 1);
    TEST_CHECK(cmd.bindDescriptorSetCount_[1] == 0);
}

static void test_render_pass_context_get_device() {
    std::vector<kale::pipeline::SubmittedDraw> draws;
    MockDevice dev;
    kale::pipeline::RenderPassContext ctx(&draws, &dev);
    TEST_CHECK(ctx.GetDevice() == &dev);

    kale::pipeline::RenderPassContext ctxNull(&draws);
    TEST_CHECK(ctxNull.GetDevice() == nullptr);
}

}  // namespace

int main() {
    test_material_bind_for_draw_with_device();
    test_material_bind_for_draw_without_device();
    test_render_pass_context_get_device();
    std::cout << "test_draw_material_binding OK" << std::endl;
    return 0;
}
