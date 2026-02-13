/**
 * @file instanced_draw.hpp
 * @brief GPU 实例化渲染：Instance Buffer、BindVertexBuffer(1)、DrawIndexed(indexCount, instanceCount)
 *
 * 与 phase10-10.6、rendering_pipeline_layer_todolist 4.4 对齐。
 * 使用前需确保材质的 Pipeline 已配置顶点 binding 1（per-instance，如 mat4）。
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_pipeline/material.hpp>
#include <kale_resource/resource_types.hpp>

namespace kale::pipeline {

/**
 * 创建用于 GPU Instancing 的 Instance Buffer。
 * 数据将上传到 GPU，调用方负责在合适时机 DestroyBuffer。
 *
 * @param device 渲染设备，为 nullptr 时返回无效句柄
 * @param data   per-instance 数据（如 glm::mat4 数组），可为 nullptr（分配空缓冲）
 * @param size   字节大小（通常为 instanceStride * instanceCount）
 * @return BufferHandle 供 BindVertexBuffer(1, handle) 使用；失败返回无效句柄
 */
kale_device::BufferHandle CreateInstanceBuffer(kale_device::IRenderDevice* device,
                                              const void* data,
                                              std::size_t size);

/**
 * 执行一次 GPU 实例化绘制：同一 Mesh + Material，多实例。
 * 绑定 Pipeline 与材质级 DescriptorSet(0)，不绑定 per-instance DescriptorSet(1)；
 * 绑定 VertexBuffer(0)=mesh、VertexBuffer(1)=instanceBuffer，再 DrawIndexed(indexCount, instanceCount)。
 *
 * 前置条件：Material 的 Pipeline 须包含顶点 binding 1（perInstance=true，如 mat4 位于 location 4-7）。
 *
 * @param cmd               命令列表
 * @param device            用于 BindForDraw(cmd, device, nullptr, 0)，可为 nullptr
 * @param mesh              网格（需有效 vertexBuffer；有 indexBuffer 时走 DrawIndexed）
 * @param material          材质（BindForDraw 仅绑定 pipeline + set 0）
 * @param instanceBuffer    实例数据缓冲（CreateInstanceBuffer 或应用层创建）
 * @param instanceBufferOffset 实例缓冲内偏移字节
 * @param instanceCount     实例数量
 */
void DrawInstanced(kale_device::CommandList& cmd,
                   kale_device::IRenderDevice* device,
                   const kale::resource::Mesh* mesh,
                   kale::pipeline::Material* material,
                   kale_device::BufferHandle instanceBuffer,
                   std::size_t instanceBufferOffset,
                   std::uint32_t instanceCount);

}  // namespace kale::pipeline
