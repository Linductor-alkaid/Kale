/**
 * @file rg_types.hpp
 * @brief Render Graph 内部类型：RG 资源句柄等
 *
 * 与 rendering_pipeline_layer_design.md 5.2、附录 A.1 对齐。
 * phase6-6.7：PassFlags 与 RG 资源句柄。
 *
 * PassFlags 使用 kale::scene::PassFlags（见 scene_types.hpp），
 * 不在此重复定义。
 */

#pragma once

#include <cstdint>

namespace kale::pipeline {

/**
 * Render Graph 内部逻辑资源句柄（纹理/缓冲等在 RG 声明阶段的 ID）。
 * Compile 时映射为 RDI 的 TextureHandle 或 BufferHandle；
 * 具体映射由 RenderGraph::Compile() 在 phase6-6.11 中实现。
 */
using RGResourceHandle = std::uint64_t;

/** 无效的 RG 资源句柄，表示未声明或未分配 */
constexpr RGResourceHandle kInvalidRGResourceHandle = 0;

/**
 * 渲染 Pass 句柄（AddPass 返回的索引/句柄）。
 * 用于 Compile/Execute 阶段按拓扑序访问 Pass。
 */
using RenderPassHandle = std::uint32_t;

/** 无效的 Pass 句柄 */
constexpr RenderPassHandle kInvalidRenderPassHandle = ~static_cast<RenderPassHandle>(0);

}  // namespace kale::pipeline
