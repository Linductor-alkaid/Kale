/**
 * @file render_pass_builder.hpp
 * @brief 渲染 Pass 构建器：在 Setup 回调中声明 Pass 的读/写依赖
 *
 * 与 rendering_pipeline_layer_design.md 5.2 对齐。
 * phase6-6.8：RenderPassBuilder。
 *
 * 通过 WriteColor/WriteDepth/ReadTexture/WriteSwapchain 声明依赖，
 * Compile 时（phase6-6.11）用于 Pass 依赖分析与资源分配。
 */

#pragma once

#include <kale_pipeline/rg_types.hpp>
#include <vector>
#include <utility>

namespace kale::pipeline {

/**
 * 渲染 Pass 构建器。
 * 在 RenderGraph::AddPass 的 Setup 回调中使用，声明本 Pass 读写的 RG 资源，
 * 供 Compile 阶段推导 Pass 顺序与屏障。
 */
class RenderPassBuilder {
public:
    /** 声明向指定 color slot 写入纹理（Render Target）。 */
    void WriteColor(uint32_t slot, RGResourceHandle texture) {
        colorOutputs_.emplace_back(slot, texture);
    }

    /** 声明写入深度附件。 */
    void WriteDepth(RGResourceHandle texture) {
        depthOutput_ = texture;
    }

    /** 声明只读采样纹理（建立读依赖）。 */
    void ReadTexture(RGResourceHandle texture) {
        readTextures_.push_back(texture);
    }

    /** 声明写入当前 back buffer（Swapchain）。 */
    void WriteSwapchain() {
        writesSwapchain_ = true;
    }

    // --- 供 RenderGraph::Compile 等内部使用的访问接口 ---

    /** 已声明的 color 输出：(slot, handle) 列表 */
    const std::vector<std::pair<uint32_t, RGResourceHandle>>& GetColorOutputs() const {
        return colorOutputs_;
    }

    /** 已声明的 depth 输出句柄，未声明则为 kInvalidRGResourceHandle */
    RGResourceHandle GetDepthOutput() const { return depthOutput_; }

    /** 已声明的只读纹理列表 */
    const std::vector<RGResourceHandle>& GetReadTextures() const { return readTextures_; }

    /** 是否声明了写入 Swapchain */
    bool WritesSwapchain() const { return writesSwapchain_; }

private:
    std::vector<std::pair<uint32_t, RGResourceHandle>> colorOutputs_;
    RGResourceHandle depthOutput_ = kInvalidRGResourceHandle;
    std::vector<RGResourceHandle> readTextures_;
    bool writesSwapchain_ = false;
};

}  // namespace kale::pipeline
