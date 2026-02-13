/**
 * @file test_pass_dependency_derivation.cpp
 * @brief phase13-13.18 Pass 依赖推导完善单元测试
 *
 * 覆盖：ReadTexture(A) 且 WriteTexture(A) 时写者先于读者；
 * 同一纹理多写者按 AddPass 顺序显式排序；
 * topologicalOrder_ 正确反映依赖；存在环时返回空。
 */

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>

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

class MockDevice : public kale_device::IRenderDevice {
public:
    bool Initialize(const kale_device::DeviceConfig&) override { return true; }
    void Shutdown() override {}
    const std::string& GetLastError() const override { return err_; }
    kale_device::BufferHandle CreateBuffer(const kale_device::BufferDesc&, const void*) override { return {}; }
    kale_device::TextureHandle CreateTexture(const kale_device::TextureDesc&, const void*) override {
        nextTexId_++;
        kale_device::TextureHandle h;
        h.id = nextTexId_;
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
    kale_device::CommandList* BeginCommandList(std::uint32_t) override { return &mockCmd_; }
    void EndCommandList(kale_device::CommandList*) override {}
    void Submit(const std::vector<kale_device::CommandList*>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                const std::vector<kale_device::SemaphoreHandle>&,
                kale_device::FenceHandle) override {}
    void WaitIdle() override {}
    kale_device::FenceHandle CreateFence(bool) override {
        nextFenceId_++;
        kale_device::FenceHandle h;
        h.id = nextFenceId_;
        return h;
    }
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
    std::uint64_t nextTexId_ = 0;
    std::uint64_t nextFenceId_ = 0;
    MockCommandList mockCmd_;
};

/** 单写单读：Pass0 写 A，Pass1 读 A → 拓扑序为 [0, 1] */
static void test_writer_before_reader() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale_device::TextureDesc texDesc;
    texDesc.width = 64;
    texDesc.height = 64;
    texDesc.format = kale_device::Format::RGBA8_UNORM;
    auto a = rg.DeclareTexture("A", texDesc);
    rg.AddPass("Writer",
        [&](kale::pipeline::RenderPassBuilder& b) { b.WriteColor(0, a); },
        [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});
    rg.AddPass("Reader",
        [&](kale::pipeline::RenderPassBuilder& b) { b.ReadTexture(a); b.WriteColor(0, a); },
        [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));
    const auto& order = rg.GetTopologicalOrder();
    TEST_CHECK(order.size() == 2u);
    TEST_CHECK(order[0] == 0);
    TEST_CHECK(order[1] == 1);
}

/** 同一纹理多写者：Pass0 写 A，Pass1 写 A，Pass2 读 A → 显式顺序 0→1→2 */
static void test_multi_writer_same_texture_explicit_order() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale_device::TextureDesc texDesc;
    texDesc.width = 64;
    texDesc.height = 64;
    texDesc.format = kale_device::Format::RGBA8_UNORM;
    auto a = rg.DeclareTexture("A", texDesc);
    rg.AddPass("W0", [&](kale::pipeline::RenderPassBuilder& b) { b.WriteColor(0, a); },
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});
    rg.AddPass("W1", [&](kale::pipeline::RenderPassBuilder& b) { b.WriteColor(0, a); },
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});
    rg.AddPass("R",  [&](kale::pipeline::RenderPassBuilder& b) { b.ReadTexture(a); },
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));
    const auto& order = rg.GetTopologicalOrder();
    TEST_CHECK(order.size() == 3u);
    size_t idx0 = 0, idx1 = 0, idx2 = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == 0) idx0 = i;
        if (order[i] == 1) idx1 = i;
        if (order[i] == 2) idx2 = i;
    }
    TEST_CHECK(idx0 < idx1 && idx1 < idx2);
}

/** 两资源链：Pass0 写 A，Pass1 写 B，Pass2 读 A 和 B → 0、1 均在 2 前 */
static void test_two_resources_reader_after_both_writers() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale_device::TextureDesc texDesc;
    texDesc.width = 64;
    texDesc.height = 64;
    texDesc.format = kale_device::Format::RGBA8_UNORM;
    auto a = rg.DeclareTexture("A", texDesc);
    auto b = rg.DeclareTexture("B", texDesc);
    rg.AddPass("WA", [&](kale::pipeline::RenderPassBuilder& x) { x.WriteColor(0, a); },
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});
    rg.AddPass("WB", [&](kale::pipeline::RenderPassBuilder& x) { x.WriteColor(0, b); },
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});
    rg.AddPass("R",  [&](kale::pipeline::RenderPassBuilder& x) {
                   x.ReadTexture(a);
                   x.ReadTexture(b);
                   x.WriteColor(0, a);
               },
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));
    const auto& order = rg.GetTopologicalOrder();
    TEST_CHECK(order.size() == 3u);
    size_t idxWA = 0, idxWB = 0, idxR = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == 0) idxWA = i;
        if (order[i] == 1) idxWB = i;
        if (order[i] == 2) idxR = i;
    }
    TEST_CHECK(idxWA < idxR && idxWB < idxR);
}

/** 循环依赖：Pass0 写 A 读 B，Pass1 写 B 读 A → Compile 失败（存在环，拓扑序为空） */
static void test_cycle_returns_empty_order() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale_device::TextureDesc texDesc;
    texDesc.width = 64;
    texDesc.height = 64;
    texDesc.format = kale_device::Format::RGBA8_UNORM;
    auto a = rg.DeclareTexture("A", texDesc);
    auto b = rg.DeclareTexture("B", texDesc);
    rg.AddPass("P0", [&](kale::pipeline::RenderPassBuilder& x) {
                   x.WriteColor(0, a);
                   x.ReadTexture(b);
               },
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});
    rg.AddPass("P1", [&](kale::pipeline::RenderPassBuilder& x) {
                   x.WriteColor(0, b);
                   x.ReadTexture(a);
               },
               [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});

    MockDevice dev;
    dev.Initialize({});
    bool compiled = rg.Compile(&dev);
    TEST_CHECK(!compiled);
    TEST_CHECK(!rg.IsCompiled());
    TEST_CHECK(!rg.GetLastError().empty());
}

}  // namespace

int main() {
    test_writer_before_reader();
    test_multi_writer_same_texture_explicit_order();
    test_two_resources_reader_after_both_writers();
    test_cycle_returns_empty_order();
    std::cout << "test_pass_dependency_derivation OK\n";
    return 0;
}
