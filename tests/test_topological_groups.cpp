/**
 * @file test_topological_groups.cpp
 * @brief phase9-9.4 Pass DAG 拓扑序分组单元测试
 *
 * 覆盖：GetTopologicalGroups() 按依赖分层；未 Compile 返回空；
 * 完整 Deferred 管线分组与拓扑序一致；同组内 Pass 无依赖；单 Pass 单组。
 */

#include <kale_pipeline/setup_render_graph.hpp>
#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstdlib>
#include <iostream>
#include <set>
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
    kale_device::TextureHandle GetBackBuffer() override {
        kale_device::TextureHandle h;
        h.id = 1;
        return h;
    }
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

static const std::vector<std::string> kExpectedDeferredPassOrder = {
    "ShadowPass",
    "GBufferPass",
    "LightingPass",
    "PostProcess",
    "OutputToSwapchain"
};

/** 未 Compile 时 GetTopologicalGroups 返回空 */
static void test_groups_uncompiled_returns_empty() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 256, 256);
    auto groups = rg.GetTopologicalGroups();
    TEST_CHECK(groups.empty());
}

/** 完整 Deferred 管线：5 个 Pass 链式依赖，应得到 5 组每组 1 个 Pass，且组序与拓扑序一致 */
static void test_deferred_groups_match_topological_order() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 1920, 1080);

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));
    TEST_CHECK(rg.IsCompiled());

    const auto& order = rg.GetTopologicalOrder();
    auto groups = rg.GetTopologicalGroups();
    const auto& passes = rg.GetPasses();

    TEST_CHECK(order.size() == kExpectedDeferredPassOrder.size());
    TEST_CHECK(!groups.empty());

    size_t totalInGroups = 0;
    for (const auto& g : groups)
        totalInGroups += g.size();
    TEST_CHECK(totalInGroups == order.size());

    size_t orderIdx = 0;
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        for (kale::pipeline::RenderPassHandle passIdx : groups[gi]) {
            TEST_CHECK(passIdx < order.size());
            TEST_CHECK(orderIdx < order.size());
            TEST_CHECK(order[orderIdx] == passIdx);
            TEST_CHECK(passes[passIdx].name == kExpectedDeferredPassOrder[orderIdx]);
            ++orderIdx;
        }
    }
    TEST_CHECK(orderIdx == order.size());
}

/** 同组内 Pass 无依赖：Deferred 链每层一个 Pass，故每组大小为 1 */
static void test_deferred_each_group_size_one() {
    kale::pipeline::RenderGraph rg;
    kale::pipeline::SetupRenderGraph(rg, 800, 600);

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));

    auto groups = rg.GetTopologicalGroups();
    TEST_CHECK(groups.size() == 5u);
    for (const auto& g : groups)
        TEST_CHECK(g.size() == 1u);
}

/** 单 Pass 图：一组且含一个 Pass */
static void test_single_pass_one_group() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale_device::TextureDesc texDescC;
    texDescC.width = 64;
    texDescC.height = 64;
    texDescC.format = kale_device::Format::RGBA8_UNORM;
    auto color = rg.DeclareTexture("C", texDescC);
    rg.AddPass("Only",
        [&](kale::pipeline::RenderPassBuilder& b) {
            b.WriteColor(0, color);
        },
        [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));

    auto groups = rg.GetTopologicalGroups();
    TEST_CHECK(groups.size() == 1u);
    TEST_CHECK(groups[0].size() == 1u);
    TEST_CHECK(rg.GetPasses()[groups[0][0]].name == "Only");
}

/** 两 Pass 无依赖：应在一组内（可并行） */
static void test_two_independent_passes_same_group() {
    kale::pipeline::RenderGraph rg;
    rg.SetResolution(64, 64);
    kale_device::TextureDesc texDesc;
    texDesc.width = 64;
    texDesc.height = 64;
    texDesc.format = kale_device::Format::RGBA8_UNORM;
    auto a = rg.DeclareTexture("A", texDesc);
    auto b = rg.DeclareTexture("B", texDesc);
    rg.AddPass("P1", [&](kale::pipeline::RenderPassBuilder& x) { x.WriteColor(0, a); }, [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});
    rg.AddPass("P2", [&](kale::pipeline::RenderPassBuilder& x) { x.WriteColor(0, b); }, [](const kale::pipeline::RenderPassContext&, kale_device::CommandList&) {});

    MockDevice dev;
    dev.Initialize({});
    TEST_CHECK(rg.Compile(&dev));

    auto groups = rg.GetTopologicalGroups();
    TEST_CHECK(groups.size() == 1u);
    TEST_CHECK(groups[0].size() == 2u);
}

}  // namespace

int main() {
    test_groups_uncompiled_returns_empty();
    test_deferred_groups_match_topological_order();
    test_deferred_each_group_size_one();
    test_single_pass_one_group();
    test_two_independent_passes_same_group();
    std::cout << "test_topological_groups OK\n";
    return 0;
}
