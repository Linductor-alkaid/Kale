/**
 * @file post_process_pass.cpp
 * @brief Tone Mapping（phase14-14.1）与 Bloom（phase14-14.2）：亮度提取、双 Pass 模糊、合成+Tone Map
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
std::uint64_t g_toneMappingDeviceId = 0;

bool g_bloomEnabled = false;
float g_bloomThreshold = 1.0f;
float g_bloomStrength = 0.04f;

kale_device::PipelineHandle g_extractBrightnessPipeline;
kale_device::DescriptorSetHandle g_extractBrightnessDescriptorSet;
kale_device::ShaderHandle g_extractBrightnessVert;
kale_device::ShaderHandle g_extractBrightnessFrag;
std::uint64_t g_extractBrightnessDeviceId = 0;

kale_device::PipelineHandle g_blurPipeline;
kale_device::DescriptorSetHandle g_blurDescriptorSet;
kale_device::ShaderHandle g_blurVert;
kale_device::ShaderHandle g_blurFrag;
std::uint64_t g_blurDeviceId = 0;

kale_device::PipelineHandle g_compositeToneMapPipeline;
kale_device::DescriptorSetHandle g_compositeToneMapDescriptorSet;
kale_device::ShaderHandle g_compositeToneMapVert;
kale_device::ShaderHandle g_compositeToneMapFrag;
std::uint64_t g_compositeToneMapDeviceId = 0;

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

static bool EnsureExtractBrightnessPipeline(kale_device::IRenderDevice* device,
                                            kale_device::TextureHandle lightingTexture) {
    if (!device || !lightingTexture.IsValid() || g_toneMappingShaderDir.empty()) return false;
    const std::uint64_t devId = reinterpret_cast<std::uint64_t>(device);
    if (g_extractBrightnessPipeline.IsValid() && g_extractBrightnessDeviceId == devId) return true;
    std::lock_guard<std::mutex> lock(g_toneMappingMutex);
    if (g_extractBrightnessPipeline.IsValid() && g_extractBrightnessDeviceId != devId) {
        device->DestroyPipeline(g_extractBrightnessPipeline);
        device->DestroyDescriptorSet(g_extractBrightnessDescriptorSet);
        device->DestroyShader(g_extractBrightnessVert);
        device->DestroyShader(g_extractBrightnessFrag);
        g_extractBrightnessPipeline = kale_device::PipelineHandle{};
        g_extractBrightnessDescriptorSet = kale_device::DescriptorSetHandle{};
        g_extractBrightnessVert = kale_device::ShaderHandle{};
        g_extractBrightnessFrag = kale_device::ShaderHandle{};
    }
    if (g_extractBrightnessPipeline.IsValid()) return true;
    std::string vp = g_toneMappingShaderDir + "/extract_brightness.vert.spv";
    std::string fp = g_toneMappingShaderDir + "/extract_brightness.frag.spv";
    auto vc = LoadFile(vp);
    auto fc = LoadFile(fp);
    if (vc.empty() || fc.empty()) return false;
    using namespace kale_device;
    ShaderDesc vd; vd.stage = ShaderStage::Vertex; vd.code = std::move(vc);
    ShaderDesc fd; fd.stage = ShaderStage::Fragment; fd.code = std::move(fc);
    g_extractBrightnessVert = device->CreateShader(vd);
    g_extractBrightnessFrag = device->CreateShader(fd);
    if (!g_extractBrightnessVert.IsValid() || !g_extractBrightnessFrag.IsValid()) {
        if (g_extractBrightnessVert.IsValid()) device->DestroyShader(g_extractBrightnessVert);
        if (g_extractBrightnessFrag.IsValid()) device->DestroyShader(g_extractBrightnessFrag);
        return false;
    }
    DescriptorSetLayoutDesc setLayout;
    setLayout.bindings = {{0, DescriptorType::CombinedImageSampler, ShaderStage::Fragment, 1}};
    PipelineDesc pipeDesc;
    pipeDesc.shaders = {g_extractBrightnessVert, g_extractBrightnessFrag};
    pipeDesc.topology = PrimitiveTopology::TriangleList;
    pipeDesc.rasterization.cullEnable = false;
    pipeDesc.depthStencil.depthTestEnable = false;
    pipeDesc.depthStencil.depthWriteEnable = false;
    pipeDesc.colorAttachmentFormats = {Format::RGBA16F};
    pipeDesc.depthAttachmentFormat = Format::Undefined;
    pipeDesc.descriptorSetLayouts = {setLayout};
    g_extractBrightnessPipeline = device->CreatePipeline(pipeDesc);
    if (!g_extractBrightnessPipeline.IsValid()) {
        device->DestroyShader(g_extractBrightnessVert);
        device->DestroyShader(g_extractBrightnessFrag);
        g_extractBrightnessVert = ShaderHandle{};
        g_extractBrightnessFrag = ShaderHandle{};
        return false;
    }
    g_extractBrightnessDescriptorSet = device->CreateDescriptorSet(setLayout);
    if (!g_extractBrightnessDescriptorSet.IsValid()) {
        device->DestroyPipeline(g_extractBrightnessPipeline);
        device->DestroyShader(g_extractBrightnessVert);
        device->DestroyShader(g_extractBrightnessFrag);
        g_extractBrightnessPipeline = PipelineHandle{};
        g_extractBrightnessVert = ShaderHandle{};
        g_extractBrightnessFrag = ShaderHandle{};
        return false;
    }
    device->WriteDescriptorSetTexture(g_extractBrightnessDescriptorSet, 0, lightingTexture);
    g_extractBrightnessDeviceId = devId;
    return true;
}

struct BlurPushConstants {
    int horizontal;
};

static bool EnsureBlurPipeline(kale_device::IRenderDevice* device,
                               kale_device::TextureHandle inputTexture) {
    if (!device || !inputTexture.IsValid() || g_toneMappingShaderDir.empty()) return false;
    const std::uint64_t devId = reinterpret_cast<std::uint64_t>(device);
    if (g_blurPipeline.IsValid() && g_blurDeviceId == devId) return true;
    std::lock_guard<std::mutex> lock(g_toneMappingMutex);
    if (g_blurPipeline.IsValid() && g_blurDeviceId != devId) {
        device->DestroyPipeline(g_blurPipeline);
        device->DestroyDescriptorSet(g_blurDescriptorSet);
        device->DestroyShader(g_blurVert);
        device->DestroyShader(g_blurFrag);
        g_blurPipeline = kale_device::PipelineHandle{};
        g_blurDescriptorSet = kale_device::DescriptorSetHandle{};
        g_blurVert = kale_device::ShaderHandle{};
        g_blurFrag = kale_device::ShaderHandle{};
    }
    if (g_blurPipeline.IsValid()) return true;
    std::string vp = g_toneMappingShaderDir + "/blur.vert.spv";
    std::string fp = g_toneMappingShaderDir + "/blur.frag.spv";
    auto vc = LoadFile(vp);
    auto fc = LoadFile(fp);
    if (vc.empty() || fc.empty()) return false;
    using namespace kale_device;
    ShaderDesc vd; vd.stage = ShaderStage::Vertex; vd.code = std::move(vc);
    ShaderDesc fd; fd.stage = ShaderStage::Fragment; fd.code = std::move(fc);
    g_blurVert = device->CreateShader(vd);
    g_blurFrag = device->CreateShader(fd);
    if (!g_blurVert.IsValid() || !g_blurFrag.IsValid()) {
        if (g_blurVert.IsValid()) device->DestroyShader(g_blurVert);
        if (g_blurFrag.IsValid()) device->DestroyShader(g_blurFrag);
        return false;
    }
    DescriptorSetLayoutDesc setLayout;
    setLayout.bindings = {{0, DescriptorType::CombinedImageSampler, ShaderStage::Fragment, 1}};
    PipelineDesc pipeDesc;
    pipeDesc.shaders = {g_blurVert, g_blurFrag};
    pipeDesc.topology = PrimitiveTopology::TriangleList;
    pipeDesc.rasterization.cullEnable = false;
    pipeDesc.depthStencil.depthTestEnable = false;
    pipeDesc.depthStencil.depthWriteEnable = false;
    pipeDesc.colorAttachmentFormats = {Format::RGBA16F};
    pipeDesc.depthAttachmentFormat = Format::Undefined;
    pipeDesc.descriptorSetLayouts = {setLayout};
    g_blurPipeline = device->CreatePipeline(pipeDesc);
    if (!g_blurPipeline.IsValid()) {
        device->DestroyShader(g_blurVert);
        device->DestroyShader(g_blurFrag);
        g_blurVert = ShaderHandle{};
        g_blurFrag = ShaderHandle{};
        return false;
    }
    g_blurDescriptorSet = device->CreateDescriptorSet(setLayout);
    if (!g_blurDescriptorSet.IsValid()) {
        device->DestroyPipeline(g_blurPipeline);
        device->DestroyShader(g_blurVert);
        device->DestroyShader(g_blurFrag);
        g_blurPipeline = PipelineHandle{};
        g_blurVert = ShaderHandle{};
        g_blurFrag = ShaderHandle{};
        return false;
    }
    device->WriteDescriptorSetTexture(g_blurDescriptorSet, 0, inputTexture);
    g_blurDeviceId = devId;
    return true;
}

struct CompositePushConstants {
    float exposure;
    float bloomStrength;
};

static bool EnsureCompositeToneMapPipeline(kale_device::IRenderDevice* device,
                                           kale_device::TextureHandle lightingTexture,
                                           kale_device::TextureHandle bloomTexture) {
    if (!device || !lightingTexture.IsValid() || !bloomTexture.IsValid() || g_toneMappingShaderDir.empty())
        return false;
    const std::uint64_t devId = reinterpret_cast<std::uint64_t>(device);
    if (g_compositeToneMapPipeline.IsValid() && g_compositeToneMapDeviceId == devId) return true;
    std::lock_guard<std::mutex> lock(g_toneMappingMutex);
    if (g_compositeToneMapPipeline.IsValid() && g_compositeToneMapDeviceId != devId) {
        device->DestroyPipeline(g_compositeToneMapPipeline);
        device->DestroyDescriptorSet(g_compositeToneMapDescriptorSet);
        device->DestroyShader(g_compositeToneMapVert);
        device->DestroyShader(g_compositeToneMapFrag);
        g_compositeToneMapPipeline = kale_device::PipelineHandle{};
        g_compositeToneMapDescriptorSet = kale_device::DescriptorSetHandle{};
        g_compositeToneMapVert = kale_device::ShaderHandle{};
        g_compositeToneMapFrag = kale_device::ShaderHandle{};
    }
    if (g_compositeToneMapPipeline.IsValid()) return true;
    std::string vp = g_toneMappingShaderDir + "/composite_tone_map.vert.spv";
    std::string fp = g_toneMappingShaderDir + "/composite_tone_map.frag.spv";
    auto vc = LoadFile(vp);
    auto fc = LoadFile(fp);
    if (vc.empty() || fc.empty()) return false;
    using namespace kale_device;
    ShaderDesc vd; vd.stage = ShaderStage::Vertex; vd.code = std::move(vc);
    ShaderDesc fd; fd.stage = ShaderStage::Fragment; fd.code = std::move(fc);
    g_compositeToneMapVert = device->CreateShader(vd);
    g_compositeToneMapFrag = device->CreateShader(fd);
    if (!g_compositeToneMapVert.IsValid() || !g_compositeToneMapFrag.IsValid()) {
        if (g_compositeToneMapVert.IsValid()) device->DestroyShader(g_compositeToneMapVert);
        if (g_compositeToneMapFrag.IsValid()) device->DestroyShader(g_compositeToneMapFrag);
        return false;
    }
    DescriptorSetLayoutDesc setLayout;
    setLayout.bindings = {
        {0, DescriptorType::CombinedImageSampler, ShaderStage::Fragment, 1},
        {1, DescriptorType::CombinedImageSampler, ShaderStage::Fragment, 1},
    };
    PipelineDesc pipeDesc;
    pipeDesc.shaders = {g_compositeToneMapVert, g_compositeToneMapFrag};
    pipeDesc.topology = PrimitiveTopology::TriangleList;
    pipeDesc.rasterization.cullEnable = false;
    pipeDesc.depthStencil.depthTestEnable = false;
    pipeDesc.depthStencil.depthWriteEnable = false;
    pipeDesc.colorAttachmentFormats = {Format::RGBA8_UNORM};
    pipeDesc.depthAttachmentFormat = Format::Undefined;
    pipeDesc.descriptorSetLayouts = {setLayout};
    g_compositeToneMapPipeline = device->CreatePipeline(pipeDesc);
    if (!g_compositeToneMapPipeline.IsValid()) {
        device->DestroyShader(g_compositeToneMapVert);
        device->DestroyShader(g_compositeToneMapFrag);
        g_compositeToneMapVert = ShaderHandle{};
        g_compositeToneMapFrag = ShaderHandle{};
        return false;
    }
    g_compositeToneMapDescriptorSet = device->CreateDescriptorSet(setLayout);
    if (!g_compositeToneMapDescriptorSet.IsValid()) {
        device->DestroyPipeline(g_compositeToneMapPipeline);
        device->DestroyShader(g_compositeToneMapVert);
        device->DestroyShader(g_compositeToneMapFrag);
        g_compositeToneMapPipeline = PipelineHandle{};
        g_compositeToneMapVert = ShaderHandle{};
        g_compositeToneMapFrag = ShaderHandle{};
        return false;
    }
    device->WriteDescriptorSetTexture(g_compositeToneMapDescriptorSet, 0, lightingTexture);
    device->WriteDescriptorSetTexture(g_compositeToneMapDescriptorSet, 1, bloomTexture);
    g_compositeToneMapDeviceId = devId;
    return true;
}

}  // namespace

void SetBloomEnabled(bool enable) { g_bloomEnabled = enable; }
bool IsBloomEnabled() { return g_bloomEnabled; }
void SetBloomThreshold(float threshold) { g_bloomThreshold = threshold; }
void SetBloomStrength(float strength) { g_bloomStrength = strength; }
float GetBloomThreshold() { return g_bloomThreshold; }
float GetBloomStrength() { return g_bloomStrength; }

void SetToneMappingShaderDirectory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(g_toneMappingMutex);
    g_toneMappingShaderDir = directory;
    g_toneMappingPipeline = kale_device::PipelineHandle{};
    g_toneMappingDescriptorSet = kale_device::DescriptorSetHandle{};
    g_toneMappingVertShader = kale_device::ShaderHandle{};
    g_toneMappingFragShader = kale_device::ShaderHandle{};
    g_toneMappingDeviceId = 0;
    g_extractBrightnessPipeline = kale_device::PipelineHandle{};
    g_extractBrightnessDescriptorSet = kale_device::DescriptorSetHandle{};
    g_extractBrightnessVert = kale_device::ShaderHandle{};
    g_extractBrightnessFrag = kale_device::ShaderHandle{};
    g_extractBrightnessDeviceId = 0;
    g_blurPipeline = kale_device::PipelineHandle{};
    g_blurDescriptorSet = kale_device::DescriptorSetHandle{};
    g_blurVert = kale_device::ShaderHandle{};
    g_blurFrag = kale_device::ShaderHandle{};
    g_blurDeviceId = 0;
    g_compositeToneMapPipeline = kale_device::PipelineHandle{};
    g_compositeToneMapDescriptorSet = kale_device::DescriptorSetHandle{};
    g_compositeToneMapVert = kale_device::ShaderHandle{};
    g_compositeToneMapFrag = kale_device::ShaderHandle{};
    g_compositeToneMapDeviceId = 0;
}

void ExecutePostProcessPass(const RenderPassContext& ctx,
                            kale_device::CommandList& cmd,
                            RGResourceHandle lightingTextureHandle,
                            RGResourceHandle bloomTextureHandle) {
    kale_device::IRenderDevice* device = ctx.GetDevice();
    if (!device) return;

    kale_device::TextureHandle lightingTex = ctx.GetCompiledTexture(lightingTextureHandle);
    if (!lightingTex.IsValid()) return;

    if (bloomTextureHandle != kInvalidRGResourceHandle) {
        kale_device::TextureHandle bloomTex = ctx.GetCompiledTexture(bloomTextureHandle);
        if (bloomTex.IsValid() && EnsureCompositeToneMapPipeline(device, lightingTex, bloomTex)) {
            CompositePushConstants pc;
            pc.exposure = 1.0f;
            pc.bloomStrength = g_bloomStrength;
            cmd.SetPushConstants(&pc, sizeof(pc), 0);
            cmd.BindPipeline(g_compositeToneMapPipeline);
            cmd.BindDescriptorSet(0, g_compositeToneMapDescriptorSet);
            cmd.Draw(3);
            return;
        }
    }

    if (!EnsureToneMappingPipeline(device, lightingTex)) return;
    float exposure = 1.0f;
    cmd.SetPushConstants(&exposure, sizeof(exposure), 0);
    cmd.BindPipeline(g_toneMappingPipeline);
    cmd.BindDescriptorSet(0, g_toneMappingDescriptorSet);
    cmd.Draw(3);
}

void ExecuteExtractBrightnessPass(const RenderPassContext& ctx,
                                  kale_device::CommandList& cmd,
                                  RGResourceHandle lightingTextureHandle) {
    kale_device::IRenderDevice* device = ctx.GetDevice();
    if (!device) return;
    kale_device::TextureHandle lightingTex = ctx.GetCompiledTexture(lightingTextureHandle);
    if (!lightingTex.IsValid()) return;
    if (!EnsureExtractBrightnessPipeline(device, lightingTex)) return;
    cmd.SetPushConstants(&g_bloomThreshold, sizeof(g_bloomThreshold), 0);
    cmd.BindPipeline(g_extractBrightnessPipeline);
    cmd.BindDescriptorSet(0, g_extractBrightnessDescriptorSet);
    cmd.Draw(3);
}

void ExecuteBloomBlurHPass(const RenderPassContext& ctx,
                           kale_device::CommandList& cmd,
                           RGResourceHandle inputTextureHandle) {
    kale_device::IRenderDevice* device = ctx.GetDevice();
    if (!device) return;
    kale_device::TextureHandle inputTex = ctx.GetCompiledTexture(inputTextureHandle);
    if (!inputTex.IsValid()) return;
    if (!EnsureBlurPipeline(device, inputTex)) return;
    BlurPushConstants pc;
    pc.horizontal = 1;
    cmd.SetPushConstants(&pc, sizeof(pc), 0);
    cmd.BindPipeline(g_blurPipeline);
    cmd.BindDescriptorSet(0, g_blurDescriptorSet);
    cmd.Draw(3);
}

void ExecuteBloomBlurVPass(const RenderPassContext& ctx,
                          kale_device::CommandList& cmd,
                          RGResourceHandle inputTextureHandle) {
    kale_device::IRenderDevice* device = ctx.GetDevice();
    if (!device) return;
    kale_device::TextureHandle inputTex = ctx.GetCompiledTexture(inputTextureHandle);
    if (!inputTex.IsValid()) return;
    if (!EnsureBlurPipeline(device, inputTex)) return;
    BlurPushConstants pc;
    pc.horizontal = 0;
    cmd.SetPushConstants(&pc, sizeof(pc), 0);
    cmd.BindPipeline(g_blurPipeline);
    cmd.BindDescriptorSet(0, g_blurDescriptorSet);
    cmd.Draw(3);
}

}  // namespace kale::pipeline
