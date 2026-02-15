/**
 * @file post_process_pass.cpp
 * @brief Tone Mapping 实现：全屏三角形 + Reinhard，可选曝光（phase14-14.1）
 */

#include <kale_pipeline/post_process_pass.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace kale::pipeline {

namespace {

std::string g_toneMappingShaderDir;
std::mutex g_toneMappingMutex;
kale_device::PipelineHandle g_toneMappingPipeline;
kale_device::DescriptorSetHandle g_toneMappingDescriptorSet;
kale_device::ShaderHandle g_toneMappingVertShader;
kale_device::ShaderHandle g_toneMappingFragShader;
std::uint64_t g_toneMappingDeviceId = 0;  // 设备切换时重建

static std::vector<std::uint8_t> LoadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto size = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<std::uint8_t> code(size);
    if (!f.read(reinterpret_cast<char*>(code.data()), size)) return {};
    return code;
}

static bool EnsureToneMappingPipeline(kale_device::IRenderDevice* device,
                                      kale_device::TextureHandle lightingTexture) {
    if (!device || !lightingTexture.IsValid() || g_toneMappingShaderDir.empty())
        return false;

    const std::uint64_t devId = reinterpret_cast<std::uint64_t>(device);
    if (g_toneMappingPipeline.IsValid() && g_toneMappingDeviceId == devId)
        return true;

    std::lock_guard<std::mutex> lock(g_toneMappingMutex);
    if (g_toneMappingPipeline.IsValid() && g_toneMappingDeviceId != devId) {
        device->DestroyPipeline(g_toneMappingPipeline);
        device->DestroyDescriptorSet(g_toneMappingDescriptorSet);
        device->DestroyShader(g_toneMappingVertShader);
        device->DestroyShader(g_toneMappingFragShader);
        g_toneMappingPipeline = kale_device::PipelineHandle{};
        g_toneMappingDescriptorSet = kale_device::DescriptorSetHandle{};
        g_toneMappingVertShader = kale_device::ShaderHandle{};
        g_toneMappingFragShader = kale_device::ShaderHandle{};
    }
    if (g_toneMappingPipeline.IsValid()) return true;

    std::string vertPath = g_toneMappingShaderDir + "/tone_mapping.vert.spv";
    std::string fragPath = g_toneMappingShaderDir + "/tone_mapping.frag.spv";
    auto vertCode = LoadFile(vertPath);
    auto fragCode = LoadFile(fragPath);
    if (vertCode.empty() || fragCode.empty()) return false;

    using namespace kale_device;
    ShaderDesc vertDesc;
    vertDesc.stage = ShaderStage::Vertex;
    vertDesc.code = std::move(vertCode);
    ShaderDesc fragDesc;
    fragDesc.stage = ShaderStage::Fragment;
    fragDesc.code = std::move(fragCode);

    g_toneMappingVertShader = device->CreateShader(vertDesc);
    g_toneMappingFragShader = device->CreateShader(fragDesc);
    if (!g_toneMappingVertShader.IsValid() || !g_toneMappingFragShader.IsValid()) {
        if (g_toneMappingVertShader.IsValid()) device->DestroyShader(g_toneMappingVertShader);
        if (g_toneMappingFragShader.IsValid()) device->DestroyShader(g_toneMappingFragShader);
        return false;
    }

    DescriptorSetLayoutDesc setLayout;
    setLayout.bindings = {{0, DescriptorType::CombinedImageSampler, ShaderStage::Fragment, 1}};

    PipelineDesc pipeDesc;
    pipeDesc.shaders = {g_toneMappingVertShader, g_toneMappingFragShader};
    pipeDesc.topology = PrimitiveTopology::TriangleList;
    pipeDesc.rasterization.cullEnable = false;
    pipeDesc.depthStencil.depthTestEnable = false;
    pipeDesc.depthStencil.depthWriteEnable = false;
    pipeDesc.colorAttachmentFormats = {Format::RGBA8_UNORM};
    pipeDesc.depthAttachmentFormat = Format::Undefined;
    pipeDesc.descriptorSetLayouts = {setLayout};

    g_toneMappingPipeline = device->CreatePipeline(pipeDesc);
    if (!g_toneMappingPipeline.IsValid()) {
        device->DestroyShader(g_toneMappingVertShader);
        device->DestroyShader(g_toneMappingFragShader);
        g_toneMappingVertShader = ShaderHandle{};
        g_toneMappingFragShader = ShaderHandle{};
        return false;
    }

    g_toneMappingDescriptorSet = device->CreateDescriptorSet(setLayout);
    if (!g_toneMappingDescriptorSet.IsValid()) {
        device->DestroyPipeline(g_toneMappingPipeline);
        device->DestroyShader(g_toneMappingVertShader);
        device->DestroyShader(g_toneMappingFragShader);
        g_toneMappingPipeline = PipelineHandle{};
        g_toneMappingVertShader = ShaderHandle{};
        g_toneMappingFragShader = ShaderHandle{};
        return false;
    }

    device->WriteDescriptorSetTexture(g_toneMappingDescriptorSet, 0, lightingTexture);
    g_toneMappingDeviceId = devId;
    return true;
}

}  // namespace

void SetToneMappingShaderDirectory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(g_toneMappingMutex);
    g_toneMappingShaderDir = directory;
    g_toneMappingPipeline = kale_device::PipelineHandle{};
    g_toneMappingDescriptorSet = kale_device::DescriptorSetHandle{};
    g_toneMappingVertShader = kale_device::ShaderHandle{};
    g_toneMappingFragShader = kale_device::ShaderHandle{};
    g_toneMappingDeviceId = 0;
}

void ExecutePostProcessPass(const RenderPassContext& ctx,
                            kale_device::CommandList& cmd,
                            RGResourceHandle lightingTextureHandle) {
    kale_device::IRenderDevice* device = ctx.GetDevice();
    if (!device) return;

    kale_device::TextureHandle lightingTex = ctx.GetCompiledTexture(lightingTextureHandle);
    if (!lightingTex.IsValid()) return;

    if (!EnsureToneMappingPipeline(device, lightingTex)) return;

    float exposure = 1.0f;
    cmd.SetPushConstants(&exposure, sizeof(exposure), 0);
    cmd.BindPipeline(g_toneMappingPipeline);
    cmd.BindDescriptorSet(0, g_toneMappingDescriptorSet);
    cmd.Draw(3);
}

}  // namespace kale::pipeline
