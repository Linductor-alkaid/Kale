/**
 * @file test_tone_mapping.cpp
 * @brief phase14-14.1 Tone Mapping 单元测试
 *
 * 覆盖：SetToneMappingShaderDirectory；ExecutePostProcessPass 在 device 空、Lighting 句柄无效时不崩溃；
 * 无 shader 目录时不绘制；可选：有 shader 目录且 mock 返回有效 pipeline 时 Bind/Draw 路径。
 */

#include <kale_pipeline/post_process_pass.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

#include <cstdlib>
#include <iostream>
#include <vector>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__       \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                               \
    } while (0)

namespace {

class MockCommandList : public kale_device::CommandList {
public:
    int bindPipelineCount = 0;
    int bindDescriptorSetCount = 0;
    int drawCount = 0;
    std::uint32_t drawVertexCount = 0;

    void BeginRenderPass(const std::vector<kale_device::TextureHandle>&,
                         kale_device::TextureHandle) override {}
    void EndRenderPass() override {}
    void BindPipeline(kale_device::PipelineHandle) override { bindPipelineCount++; }
    void BindDescriptorSet(std::uint32_t, kale_device::DescriptorSetHandle) override { bindDescriptorSetCount++; }
    void BindVertexBuffer(std::uint32_t, kale_device::BufferHandle, std::size_t) override {}
    void BindIndexBuffer(kale_device::BufferHandle, std::size_t, bool) override {}
    void SetPushConstants(const void*, std::size_t, std::size_t) override {}
    void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount,
              std::uint32_t firstVertex, std::uint32_t firstInstance) override {
        drawCount++;
        drawVertexCount = vertexCount;
    }
    void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
                    std::uint32_t firstIndex, std::int32_t vertexOffset,
                    std::uint32_t firstInstance) override {}
    void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void CopyBufferToBuffer(kale_device::BufferHandle, std::size_t, kale_device::BufferHandle,
                            std::size_t, std::size_t) override {}
    void CopyBufferToTexture(kale_device::BufferHandle, std::size_t, kale_device::TextureHandle,
                             std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void CopyTextureToTexture(kale_device::TextureHandle, kale_device::TextureHandle,
                              std::uint32_t, std::uint32_t) override {}
    void Barrier(const std::vector<kale_device::TextureHandle>&) override {}
    void ClearColor(kale_device::TextureHandle, const float[4]) override {}
    void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
};

class MockDeviceWithPipeline : public kale_device::IRenderDevice {
public:
    MockCommandList mockCmd;
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override { return {}; }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override {
        kale_device::TextureHandle h;
        h.id = ++nextId_;
        return h;
    }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override {
        kale_device::ShaderHandle h;
        h.id = ++nextId_;
        return h;
    }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override {
        kale_device::PipelineHandle h;
        h.id = ++nextId_;
        return h;
    }
    kale_device::DescriptorSetHandle CreateDescriptorSet(const kale_device::DescriptorSetLayoutDesc&) override {
        kale_device::DescriptorSetHandle h;
        h.id = ++nextId_;
        return h;
    }
    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::BufferHandle,
                                  std::size_t, std::size_t) override {}

    void DestroyBuffer(kale_device::BufferHandle) override {}
    void DestroyTexture(kale_device::TextureHandle) override {}
    void DestroyShader(kale_device::ShaderHandle) override {}
    void DestroyPipeline(kale_device::PipelineHandle) override {}
    void DestroyDescriptorSet(kale_device::DescriptorSetHandle) override {}

    void UpdateBuffer(kale_device::BufferHandle, const void*, std::size_t, std::size_t) override {}
    void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override { return nullptr; }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}

    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &mockCmd; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}

    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override { kale_device::FenceHandle h; h.id = ++nextId_; return h; }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }

    std::uint32_t AcquireNextImage() override { return 0; }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { return {}; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }
    void SetExtent(std::uint32_t, std::uint32_t) override {}

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextId_ = 0;
};

static void test_set_tone_mapping_shader_directory() {
    kale::pipeline::SetToneMappingShaderDirectory("");
    kale::pipeline::SetToneMappingShaderDirectory("/nonexistent");
    // 无崩溃即通过
}

static void test_execute_post_process_pass_no_device() {
    std::vector<kale::pipeline::SubmittedDraw> draws;
    kale::pipeline::RenderPassContext ctx(&draws, nullptr, nullptr);
    MockCommandList cmd;
    kale::pipeline::ExecutePostProcessPass(ctx, cmd, kale::pipeline::kInvalidRGResourceHandle);
    kale::pipeline::ExecutePostProcessPass(ctx, cmd, 1u);
    TEST_CHECK(cmd.drawCount == 0);
}

static void test_execute_post_process_pass_invalid_lighting_handle() {
    std::vector<kale::pipeline::SubmittedDraw> draws;
    MockDeviceWithPipeline dev;
    kale::pipeline::RenderPassContext ctx(&draws, &dev, nullptr);
    MockCommandList cmd;
    kale::pipeline::SetToneMappingShaderDirectory("");
    kale::pipeline::ExecutePostProcessPass(ctx, cmd, kale::pipeline::kInvalidRGResourceHandle);
    TEST_CHECK(cmd.drawCount == 0);
}

static void test_execute_post_process_pass_with_mock_pipeline() {
    std::vector<kale::pipeline::SubmittedDraw> draws;
    MockDeviceWithPipeline dev;
    kale::pipeline::RenderPassContext ctx(&draws, &dev, nullptr);
    kale::pipeline::SetToneMappingShaderDirectory("");
    kale::pipeline::ExecutePostProcessPass(ctx, dev.mockCmd, 1u);
    TEST_CHECK(dev.mockCmd.drawCount == 0);
}

}  // namespace

int main() {
    test_set_tone_mapping_shader_directory();
    test_execute_post_process_pass_no_device();
    test_execute_post_process_pass_invalid_lighting_handle();
    test_execute_post_process_pass_with_mock_pipeline();
    std::cout << "test_tone_mapping: all passed\n";
    return 0;
}
