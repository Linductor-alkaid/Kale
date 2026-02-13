/**
 * @file test_render_pipeline_error_lifecycle.cpp
 * @brief phase13-13.21 渲染管线层错误处理与生命周期单元测试
 *
 * 覆盖：Compile 失败时 GetLastError 非空；资源分配失败时已分配资源被释放；
 * 未编译或无效 handle 时 GetCompiledTexture/GetCompiledBuffer 返回空；
 * ReleaseFrameResources 空 submittedDraws 不崩溃。
 */

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
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
                             std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void CopyTextureToTexture(kale_device::TextureHandle, kale_device::TextureHandle,
                              std::uint32_t, std::uint32_t) override {}
    void Barrier(const std::vector<kale_device::TextureHandle>&) override {}
    void ClearColor(kale_device::TextureHandle, const float*) override {}
    void ClearDepth(kale_device::TextureHandle, float, std::uint8_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
};

/** Mock 设备：可选在第二次 CreateTexture 时返回无效，用于验证失败路径下已分配资源被 Destroy。 */
class MockDeviceFailSecondTexture : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }

    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override {
        createTextureCallCount_++;
        if (createTextureCallCount_ >= 2) return {};  // 第二次及以后失败
        nextTexId_++;
        kale_device::TextureHandle h;
        h.id = nextTexId_;
        return h;
    }
    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override { return {}; }
    kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
    kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }
    kale_device::DescriptorSetHandle CreateDescriptorSet(const kale_device::DescriptorSetLayoutDesc&) override { return {}; }
    void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::TextureHandle) override {}
    void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::BufferHandle, std::size_t, std::size_t) override {}
    void DestroyBuffer(kale_device::BufferHandle) override {}
    void DestroyTexture(kale_device::TextureHandle h) override {
        if (h.IsValid()) destroyTextureCallCount_++;
    }
    void DestroyShader(kale_device::ShaderHandle) override {}
    void DestroyPipeline(kale_device::PipelineHandle) override {}
    void DestroyDescriptorSet(kale_device::DescriptorSetHandle) override {}
    void UpdateBuffer(kale_device::BufferHandle, const void*, std::size_t, std::size_t) override {}
    void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override { return nullptr; }
    void UnmapBuffer(kale_device::BufferHandle) override {}
    void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}
    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &mockCmd_; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}
    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override { kale_device::FenceHandle h; h.id = 1; return h; }
    void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
    void ResetFence(kale_device::FenceHandle) override {}
    bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
    kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }
    std::uint32_t AcquireNextImage() override { return 0; }
    void Present() override {}
    kale_device::TextureHandle GetBackBuffer() override { kale_device::TextureHandle h; h.id = 1; return h; }
    std::uint32_t GetCurrentFrameIndex() const override { return 0; }
    const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }
    void SetExtent(std::uint32_t, std::uint32_t) override {}

    int getDestroyTextureCallCount() const { return destroyTextureCallCount_; }
    void resetCounts() { createTextureCallCount_ = 0; destroyTextureCallCount_ = 0; }

private:
    std::string err_;
    kale_device::DeviceCapabilities caps_;
    std::uint64_t nextTexId_ = 0;
    int createTextureCallCount_ = 0;
    int destroyTextureCallCount_ = 0;
    MockCommandList mockCmd_;
};

/** Compile(nullptr) 返回 false 且 GetLastError 非空 */
static void test_compile_null_device() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale_device::TextureDesc texDesc;
    texDesc.width = 64;
    texDesc.height = 64;
    texDesc.format = kale_device::Format::RGBA8_UNORM;
    rg.DeclareTexture("A", texDesc);
    rg.AddPass("P", [](kale::pipeline::RenderPassBuilder&) {},
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});

    bool ok = rg.Compile(nullptr);
    TEST_CHECK(!ok);
    TEST_CHECK(!rg.GetLastError().empty());
    TEST_CHECK(!rg.IsCompiled());
}

/** 资源分配失败时已分配资源被释放：两纹理，第二份 CreateTexture 失败，应 Destroy 第一份 */
static void test_compile_failure_cleans_up_allocated() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale_device::TextureDesc texDesc;
    texDesc.width = 64;
    texDesc.height = 64;
    texDesc.format = kale_device::Format::RGBA8_UNORM;
    rg.DeclareTexture("A", texDesc);
    rg.DeclareTexture("B", texDesc);
    rg.AddPass("P0", [&](kale::pipeline::RenderPassBuilder& x) { x.WriteColor(0, 1); },
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});
    rg.AddPass("P1", [&](kale::pipeline::RenderPassBuilder& x) { x.WriteColor(0, 2); },
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});

    MockDeviceFailSecondTexture dev;
    dev.Initialize({});
    dev.resetCounts();
    bool compiled = rg.Compile(&dev);
    TEST_CHECK(!compiled);
    TEST_CHECK(!rg.GetLastError().empty());
    TEST_CHECK(dev.getDestroyTextureCallCount() >= 1);  // 至少释放了第一次成功分配的那个
}

/** 未编译时 GetCompiledTexture/GetCompiledBuffer 返回空；无效 handle 返回空 */
static void test_get_compiled_handle_when_not_compiled_or_invalid() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale_device::TextureDesc texDesc;
    texDesc.width = 64;
    texDesc.height = 64;
    texDesc.format = kale_device::Format::RGBA8_UNORM;
    auto ha = rg.DeclareTexture("A", texDesc);

    TEST_CHECK(!rg.GetCompiledTexture(kale::pipeline::kInvalidRGResourceHandle).IsValid());
    TEST_CHECK(!rg.GetCompiledTexture(0).IsValid());
    TEST_CHECK(!rg.GetCompiledTexture(ha).IsValid());  // 未 Compile，应返回空

    kale_device::BufferDesc bufDesc;
    bufDesc.size = 256;
    bufDesc.usage = kale_device::BufferUsage::Uniform;
    auto hb = rg.DeclareBuffer("B", bufDesc);
    TEST_CHECK(!rg.GetCompiledBuffer(kale::pipeline::kInvalidRGResourceHandle).IsValid());
    TEST_CHECK(!rg.GetCompiledBuffer(0).IsValid());
    TEST_CHECK(!rg.GetCompiledBuffer(hb).IsValid());
}

/** 编译成功后 GetCompiledTexture/GetCompiledBuffer 有效 handle 返回有效句柄 */
static void test_get_compiled_handle_after_compile() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale_device::TextureDesc texDesc;
    texDesc.width = 64;
    texDesc.height = 64;
    texDesc.format = kale_device::Format::RGBA8_UNORM;
    auto ha = rg.DeclareTexture("A", texDesc);
    kale_device::BufferDesc bufDesc;
    bufDesc.size = 256;
    bufDesc.usage = kale_device::BufferUsage::Uniform;
    auto hb = rg.DeclareBuffer("B", bufDesc);
    rg.AddPass("P", [](kale::pipeline::RenderPassBuilder&) {},
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});

    class SimpleMockDevice : public kale_device::IRenderDevice {
    public:
        bool Initialize(const kale_device::DeviceConfig&) override { return true; }
        void Shutdown() override {}
        const std::string& GetLastError() const override { return err_; }
        kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override {
            kale_device::TextureHandle h;
            h.id = ++nextId_;
            return h;
        }
        kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override {
            kale_device::BufferHandle h;
            h.id = ++nextId_;
            return h;
        }
        kale_device::ShaderHandle CreateShader(const kale_device::ShaderDesc&) override { return {}; }
        kale_device::PipelineHandle CreatePipeline(const kale_device::PipelineDesc&) override { return {}; }
        kale_device::DescriptorSetHandle CreateDescriptorSet(const kale_device::DescriptorSetLayoutDesc&) override { return {}; }
        void WriteDescriptorSetTexture(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::TextureHandle) override {}
        void WriteDescriptorSetBuffer(kale_device::DescriptorSetHandle, std::uint32_t, kale_device::BufferHandle, std::size_t, std::size_t) override {}
        void DestroyBuffer(kale_device::BufferHandle) override {}
        void DestroyTexture(kale_device::TextureHandle) override {}
        void DestroyShader(kale_device::ShaderHandle) override {}
        void DestroyPipeline(kale_device::PipelineHandle) override {}
        void DestroyDescriptorSet(kale_device::DescriptorSetHandle) override {}
        void UpdateBuffer(kale_device::BufferHandle, const void*, std::size_t, std::size_t) override {}
        void* MapBuffer(kale_device::BufferHandle, std::size_t, std::size_t) override { return nullptr; }
        void UnmapBuffer(kale_device::BufferHandle) override {}
        void UpdateTexture(kale_device::TextureHandle, const void*, std::uint32_t) override {}
        kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &cmd_; }
        void EndCommandList(kale_device::CommandList*) override {}
        void Submit(const std::vector<kale_device::CommandList*>&,
                    const std::vector<kale_device::SemaphoreHandle>&,
                    const std::vector<kale_device::SemaphoreHandle>&,
                    kale_device::FenceHandle) override {}
        void WaitIdle() override {}
        kale_device::FenceHandle CreateFence(bool) override { kale_device::FenceHandle h; h.id = 1; return h; }
        void WaitForFence(kale_device::FenceHandle, std::uint64_t) override {}
        void ResetFence(kale_device::FenceHandle) override {}
        bool IsFenceSignaled(kale_device::FenceHandle) const override { return true; }
        kale_device::SemaphoreHandle CreateSemaphore() override { return {}; }
        std::uint32_t AcquireNextImage() override { return 0; }
        void Present() override {}
        kale_device::TextureHandle GetBackBuffer() override { kale_device::TextureHandle h; h.id = 1; return h; }
        std::uint32_t GetCurrentFrameIndex() const override { return 0; }
        const kale_device::DeviceCapabilities& GetCapabilities() const override { return caps_; }
        void SetExtent(std::uint32_t, std::uint32_t) override {}
    private:
        std::string err_;
        kale_device::DeviceCapabilities caps_;
        std::uint64_t nextId_ = 0;
        MockCommandList cmd_;
    };
    SimpleMockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));
    TEST_CHECK(rg.GetCompiledTexture(ha).IsValid());
    TEST_CHECK(rg.GetCompiledBuffer(hb).IsValid());
}

/** ReleaseFrameResources 空 submittedDraws 不崩溃 */
static void test_release_frame_resources_empty_draws() {
    kale::pipeline::RenderGraph rg;
    rg.ReleaseFrameResources();
}

}  // namespace

int main() {
    test_compile_null_device();
    test_compile_failure_cleans_up_allocated();
    test_get_compiled_handle_when_not_compiled_or_invalid();
    test_get_compiled_handle_after_compile();
    test_release_frame_resources_empty_draws();
    std::cout << "test_render_pipeline_error_lifecycle OK\n";
    return 0;
}
