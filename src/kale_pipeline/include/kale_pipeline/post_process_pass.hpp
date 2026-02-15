/**
 * @file post_process_pass.hpp
 * @brief Post-Process Pass：FinalColor、可选 Bloom（ExtractBrightness → BlurH → BlurV → Composite+ToneMap）
 *
 * 与 rendering_pipeline_layer_design.md 2.1、phase8-8.7、phase14-14.1、phase14-14.2 对齐。
 * 依赖 Lighting Pass。Execute：Tone Mapping（Reinhard）；若启用 Bloom 则先执行亮度提取与双 Pass 模糊，再合成+Tone Map。
 * 应用层需在 Compile 前调用 SetToneMappingShaderDirectory 指向含 tone_mapping / extract_brightness / blur / composite_tone_map 的 .spv 目录。
 */

#pragma once

#include <kale_pipeline/render_graph.hpp>
#include <kale_pipeline/render_pass_builder.hpp>
#include <kale_pipeline/render_pass_context.hpp>
#include <kale_pipeline/rg_types.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <string>

namespace kale::pipeline {

/** 启用/禁用 Bloom（默认关闭）。须在 SetupPostProcessPass 前调用。 */
void SetBloomEnabled(bool enable);
/** 是否启用 Bloom。 */
bool IsBloomEnabled();
/** Bloom 亮度阈值，高于此值才参与泛光（默认 1.0）。 */
void SetBloomThreshold(float threshold);
/** Bloom 合成强度（默认 0.04）。 */
void SetBloomStrength(float strength);
/** 获取当前 Bloom 阈值。 */
float GetBloomThreshold();
/** 获取当前 Bloom 强度。 */
float GetBloomStrength();

/**
 * 设置 Tone Mapping 着色器目录（含 tone_mapping、extract_brightness、blur、composite_tone_map 的 .vert.spv/.frag.spv）。
 * 未设置或目录无效时 ExecutePostProcessPass 不绘制。
 */
void SetToneMappingShaderDirectory(const std::string& directory);

/**
 * 执行 Post-Process Pass：Tone Mapping；若传入有效 bloomTextureHandle 则使用 Composite+ToneMap（Lighting + Bloom）。
 */
void ExecutePostProcessPass(const RenderPassContext& ctx,
                            kale_device::CommandList& cmd,
                            RGResourceHandle lightingTextureHandle,
                            RGResourceHandle bloomTextureHandle = kInvalidRGResourceHandle);

/** 执行亮度提取 Pass（Bloom 链第一步）。 */
void ExecuteExtractBrightnessPass(const RenderPassContext& ctx,
                                  kale_device::CommandList& cmd,
                                  RGResourceHandle lightingTextureHandle);

/** 执行水平模糊 Pass。 */
void ExecuteBloomBlurHPass(const RenderPassContext& ctx,
                           kale_device::CommandList& cmd,
                           RGResourceHandle inputTextureHandle);

/** 执行垂直模糊 Pass。 */
void ExecuteBloomBlurVPass(const RenderPassContext& ctx,
                           kale_device::CommandList& cmd,
                           RGResourceHandle inputTextureHandle);

/**
 * 向 RenderGraph 添加 Post-Process Pass；若已 SetBloomEnabled(true) 则先添加 ExtractBrightness、BloomBlurH、BloomBlurV，再 PostProcess。
 * 声明 FinalColor；Bloom 启用时声明 BloomBright、BloomBlurA、BloomBlurB（半分辨率 RGBA16F）。
 */
inline void SetupPostProcessPass(RenderGraph& rg) {
    using namespace kale_device;

    TextureDesc finalColorDesc;
    finalColorDesc.width = 0;
    finalColorDesc.height = 0;
    finalColorDesc.format = Format::RGBA8_UNORM;
    finalColorDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::Transfer;

    RGResourceHandle finalColor = rg.DeclareTexture("FinalColor", finalColorDesc);
    RGResourceHandle lightingResult = rg.GetHandleByName("Lighting");

    if (IsBloomEnabled() && lightingResult != kInvalidRGResourceHandle) {
        std::uint32_t w = rg.GetResolutionWidth();
        std::uint32_t h = rg.GetResolutionHeight();
        std::uint32_t halfW = (w > 0) ? (w / 2) : 1u;
        std::uint32_t halfH = (h > 0) ? (h / 2) : 1u;
        TextureDesc bloomDesc;
        bloomDesc.width = halfW;
        bloomDesc.height = halfH;
        bloomDesc.format = Format::RGBA16F;
        bloomDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled;

        RGResourceHandle bloomBright = rg.DeclareTexture("BloomBright", bloomDesc);
        bloomDesc.width = halfW;
        bloomDesc.height = halfH;
        RGResourceHandle bloomBlurA = rg.DeclareTexture("BloomBlurA", bloomDesc);
        RGResourceHandle bloomBlurB = rg.DeclareTexture("BloomBlurB", bloomDesc);

        rg.AddPass(
            "ExtractBrightness",
            [lightingResult, bloomBright](RenderPassBuilder& b) {
                b.ReadTexture(lightingResult);
                b.WriteColor(0, bloomBright);
            },
            [lightingResult](const RenderPassContext& ctx, CommandList& cmd) {
                ExecuteExtractBrightnessPass(ctx, cmd, lightingResult);
            });

        rg.AddPass(
            "BloomBlurH",
            [bloomBright, bloomBlurA](RenderPassBuilder& b) {
                b.ReadTexture(bloomBright);
                b.WriteColor(0, bloomBlurA);
            },
            [bloomBright](const RenderPassContext& ctx, CommandList& cmd) {
                ExecuteBloomBlurHPass(ctx, cmd, bloomBright);
            });

        rg.AddPass(
            "BloomBlurV",
            [bloomBlurA, bloomBlurB](RenderPassBuilder& b) {
                b.ReadTexture(bloomBlurA);
                b.WriteColor(0, bloomBlurB);
            },
            [bloomBlurA](const RenderPassContext& ctx, CommandList& cmd) {
                ExecuteBloomBlurVPass(ctx, cmd, bloomBlurA);
            });

        rg.AddPass(
            "PostProcess",
            [finalColor, lightingResult, bloomBlurB](RenderPassBuilder& b) {
                b.WriteColor(0, finalColor);
                b.ReadTexture(lightingResult);
                b.ReadTexture(bloomBlurB);
            },
            [lightingResult, bloomBlurB](const RenderPassContext& ctx, CommandList& cmd) {
                ExecutePostProcessPass(ctx, cmd, lightingResult, bloomBlurB);
            });
    } else {
        rg.AddPass(
            "PostProcess",
            [finalColor, lightingResult](RenderPassBuilder& b) {
                b.WriteColor(0, finalColor);
                if (lightingResult != kInvalidRGResourceHandle)
                    b.ReadTexture(lightingResult);
            },
            [lightingResult](const RenderPassContext& ctx, CommandList& cmd) {
                ExecutePostProcessPass(ctx, cmd, lightingResult, kInvalidRGResourceHandle);
            });
    }
}

}  // namespace kale::pipeline
