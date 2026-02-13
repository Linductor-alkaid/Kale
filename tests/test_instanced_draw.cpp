/**
 * @file test_instanced_draw.cpp
 * @brief phase10-10.6 GPU Instancing 单元测试
 *
 * 覆盖：CreateInstanceBuffer(null/空 size 返回无效、有效创建返回有效句柄)；
 * DrawInstanced(null mesh/material/device/instanceBuffer 不崩溃、instanceCount=0 不绘制、
 * BindVertexBuffer(1, instanceBuffer) 与 DrawIndexed(indexCount, instanceCount) 被调用)。
 */

#include <kale_pipeline/instanced_draw.hpp>
#include <kale_pipeline/material.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

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

/** Mock CommandList：记录 BindVertexBuffer(binding=1) 与 DrawIndexed 的 instanceCount */
class MockCommandList : public kale_device::CommandList {
public:
    void BeginRenderPass(const std::vector<kale_device::TextureHandle>&,
                         kale_device::TextureHandle) override {}
    void EndRenderPass() override {}
    void BindPipeline(kale_device::PipelineHandle) override {}
    void BindDescriptorSet(std::uint32_t, kale_device::DescriptorSetHandle) override {}
    void BindVertexBuffer(std::uint32_t binding, kale_device::BufferHandle buffer,
                          std::size_t offset) override {
        if (binding == 1) {
            bindVertexBuffer1Count_++;
            lastInstanceBuffer_ = buffer;
            lastInstanceBufferOffset_ = offset;
        }
    }
    void BindIndexBuffer(kale_device::BufferHandle, std::size_t, bool) override {}
    void SetPushConstants(const void*, std::size_t, std::size_t) override {}
    void Draw(std::uint32_t, std::uint32_t instanceCount, std::uint32_t, std::uint32_t) override {
        lastDrawInstanceCount_ = instanceCount;
    }
    void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
                     std::uint32_t, std::int32_t, std::uint32_t) override {
        lastDrawIndexedIndexCount_ = indexCount;
        lastDrawIndexedInstanceCount_ = instanceCount;
    }
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

    int bindVertexBuffer1Count_ = 0;
    kale_device::BufferHandle lastInstanceBuffer_{};
    std::size_t lastInstanceBufferOffset_ = 0;
    std::uint32_t lastDrawInstanceCount_ = 0;
    std::uint32_t lastDrawIndexedIndexCount_ = 0;
    std::uint32_t lastDrawIndexedInstanceCount_ = 0;
};

/** Mock 设备：CreateBuffer 返回有效句柄 */
class MockDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc& desc,
                                          const void*) override {
        kale_device::BufferHandle h;
        if (desc.size > 0)
            h.id = ++nextId_;
        return h;
    }
    void DestroyBuffer(kale_device::BufferHandle) override {}
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override { return {}; }
    void DestroyTexture(kale_device::TextureHandle) override {}
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
    void DestroyShader(kale_device::ShaderHandle) override {}
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }
    void DestroyPipeline(kale_device::PipelineHandle) override {}
    kale_device::DescriptorSetHandle CreateDescriptorSet(
        const kale_device::DescriptorSetLayoutDesc&) override { return {}; }
    void DestroyDescriptorSet(kale_device::DescriptorSetHandle) override {}
    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t,
                                   kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t,
                                  kale_device::BufferHandle, std::size_t, std::size_t) override {}
    kale_device::DescriptorSetHandle AcquireInstanceDescriptorSet(const void*, std::size_t) override { return {}; }
    void ReleaseInstanceDescriptorSet(kale_device::DescriptorSetHandle) override {}

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

static void test_create_instance_buffer_null_device() {
    kale_device::BufferHandle h = kale::pipeline::CreateInstanceBuffer(nullptr, nullptr, 64);
    TEST_CHECK(!h.IsValid());
}

static void test_create_instance_buffer_zero_size() {
    MockDevice dev;
    dev.Initialize({});
    kale_device::BufferHandle h = kale::pipeline::CreateInstanceBuffer(&dev, nullptr, 0);
    TEST_CHECK(!h.IsValid());
}

static void test_create_instance_buffer_valid() {
    MockDevice dev;
    dev.Initialize({});
    glm::mat4 mats[3] = { glm::mat4(1.f), glm::mat4(1.f), glm::mat4(1.f) };
    kale_device::BufferHandle h = kale::pipeline::CreateInstanceBuffer(
        &dev, mats, sizeof(mats));
    TEST_CHECK(h.IsValid());
}

static void test_draw_instanced_instance_count_zero() {
    MockCommandList cmd;
    MockDevice dev;
    dev.Initialize({});
    kale::resource::Mesh mesh;
    mesh.vertexBuffer.id = 1;
    mesh.vertexCount = 3;
    mesh.indexBuffer.id = 1;
    mesh.indexCount = 6;
    kale::pipeline::Material mat;
    kale_device::BufferHandle instanceBuf;
    instanceBuf.id = 10;

    kale::pipeline::DrawInstanced(cmd, &dev, &mesh, &mat, instanceBuf, 0, 0);

    TEST_CHECK(cmd.bindVertexBuffer1Count_ == 0);
    TEST_CHECK(cmd.lastDrawIndexedInstanceCount_ == 0);
}

static void test_draw_instanced_null_mesh() {
    MockCommandList cmd;
    MockDevice dev;
    dev.Initialize({});
    kale::pipeline::Material mat;
    kale_device::BufferHandle instanceBuf;
    instanceBuf.id = 10;

    kale::pipeline::DrawInstanced(cmd, &dev, nullptr, &mat, instanceBuf, 0, 3);

    TEST_CHECK(cmd.bindVertexBuffer1Count_ == 0);
}

static void test_draw_instanced_null_instance_buffer() {
    MockCommandList cmd;
    MockDevice dev;
    dev.Initialize({});
    kale::resource::Mesh mesh;
    mesh.vertexBuffer.id = 1;
    mesh.vertexCount = 3;
    mesh.indexBuffer.id = 1;
    mesh.indexCount = 6;
    kale::pipeline::Material mat;

    kale::pipeline::DrawInstanced(cmd, &dev, &mesh, &mat, kale_device::BufferHandle{}, 0, 3);

    TEST_CHECK(cmd.bindVertexBuffer1Count_ == 0);
}

static void test_draw_instanced_success_indexed() {
    MockCommandList cmd;
    MockDevice dev;
    dev.Initialize({});
    kale::resource::Mesh mesh;
    mesh.vertexBuffer.id = 1;
    mesh.vertexCount = 12;
    mesh.indexBuffer.id = 2;
    mesh.indexCount = 18;
    kale::pipeline::Material mat;
    kale_device::PipelineHandle ph;
    ph.id = 1;
    mat.SetPipeline(ph);
    kale_device::BufferHandle instanceBuf;
    instanceBuf.id = 10;

    kale::pipeline::DrawInstanced(cmd, &dev, &mesh, &mat, instanceBuf, 0, 5);

    TEST_CHECK(cmd.bindVertexBuffer1Count_ == 1);
    TEST_CHECK(cmd.lastInstanceBuffer_.id == 10);
    TEST_CHECK(cmd.lastInstanceBufferOffset_ == 0);
    TEST_CHECK(cmd.lastDrawIndexedIndexCount_ == 18);
    TEST_CHECK(cmd.lastDrawIndexedInstanceCount_ == 5);
}

static void test_draw_instanced_success_non_indexed() {
    MockCommandList cmd;
    MockDevice dev;
    dev.Initialize({});
    kale::resource::Mesh mesh;
    mesh.vertexBuffer.id = 1;
    mesh.vertexCount = 9;
    mesh.indexBuffer = kale_device::BufferHandle{};
    mesh.indexCount = 0;
    kale::pipeline::Material mat;
    kale_device::BufferHandle instanceBuf;
    instanceBuf.id = 20;

    kale::pipeline::DrawInstanced(cmd, &dev, &mesh, &mat, instanceBuf, 256, 2);

    TEST_CHECK(cmd.bindVertexBuffer1Count_ == 1);
    TEST_CHECK(cmd.lastInstanceBuffer_.id == 20);
    TEST_CHECK(cmd.lastInstanceBufferOffset_ == 256);
    TEST_CHECK(cmd.lastDrawInstanceCount_ == 2);
}

static void test_draw_instanced_null_material_no_crash() {
    MockCommandList cmd;
    MockDevice dev;
    dev.Initialize({});
    kale::resource::Mesh mesh;
    mesh.vertexBuffer.id = 1;
    mesh.vertexCount = 3;
    mesh.indexBuffer.id = 1;
    mesh.indexCount = 6;
    kale_device::BufferHandle instanceBuf;
    instanceBuf.id = 10;

    kale::pipeline::DrawInstanced(cmd, &dev, &mesh, nullptr, instanceBuf, 0, 2);

    TEST_CHECK(cmd.bindVertexBuffer1Count_ == 1);
    TEST_CHECK(cmd.lastDrawIndexedInstanceCount_ == 2);
}

}  // namespace

int main() {
    test_create_instance_buffer_null_device();
    test_create_instance_buffer_zero_size();
    test_create_instance_buffer_valid();
    test_draw_instanced_instance_count_zero();
    test_draw_instanced_null_mesh();
    test_draw_instanced_null_instance_buffer();
    test_draw_instanced_success_indexed();
    test_draw_instanced_success_non_indexed();
    test_draw_instanced_null_material_no_crash();
    std::cout << "test_instanced_draw OK" << std::endl;
    return 0;
}
