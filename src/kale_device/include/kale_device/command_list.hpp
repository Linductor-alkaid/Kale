/**
 * @file command_list.hpp
 * @brief CommandList 命令列表纯虚接口
 *
 * 用于录制渲染/计算命令：Render Pass、管线绑定、Draw、Dispatch、Barrier、Clear、Viewport/Scissor。
 * 与 device_abstraction_layer_design.md 5.1.5 对齐。
 */

#pragma once

#include <cstdint>
#include <vector>

#include <kale_device/rdi_types.hpp>

namespace kale_device {

/**
 * 命令列表抽象接口。
 * 由 IRenderDevice::BeginCommandList() 返回，录制完成后由 EndCommandList() 结束。
 * 后端实现（如 VulkanCommandList）封装 VkCommandBuffer 或等价的命令队列。
 */
class CommandList {
public:
    virtual ~CommandList() = default;

    // -------------------------------------------------------------------------
    // Render Pass
    // -------------------------------------------------------------------------

    /** 开始一次 Render Pass，绑定 color 与可选的 depth 附件 */
    virtual void BeginRenderPass(const std::vector<TextureHandle>& colorAttachments,
                                 TextureHandle depthAttachment = {}) = 0;
    /** 结束当前 Render Pass */
    virtual void EndRenderPass() = 0;

    // -------------------------------------------------------------------------
    // Pipeline Binding
    // -------------------------------------------------------------------------

    virtual void BindPipeline(PipelineHandle pipeline) = 0;
    virtual void BindDescriptorSet(std::uint32_t set, DescriptorSetHandle descriptorSet) = 0;

    // -------------------------------------------------------------------------
    // Resource Binding
    // -------------------------------------------------------------------------

    virtual void BindVertexBuffer(std::uint32_t binding, BufferHandle buffer,
                                  std::size_t offset = 0) = 0;
    virtual void BindIndexBuffer(BufferHandle buffer, std::size_t offset = 0,
                                 bool is16Bit = false) = 0;

    // -------------------------------------------------------------------------
    // Push Constants
    // -------------------------------------------------------------------------

    virtual void SetPushConstants(const void* data, std::size_t size,
                                  std::size_t offset = 0) = 0;

    // -------------------------------------------------------------------------
    // Draw
    // -------------------------------------------------------------------------

    virtual void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1,
                      std::uint32_t firstVertex = 0, std::uint32_t firstInstance = 0) = 0;
    virtual void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1,
                             std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0,
                             std::uint32_t firstInstance = 0) = 0;

    // -------------------------------------------------------------------------
    // Compute
    // -------------------------------------------------------------------------

    virtual void Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                          std::uint32_t groupCountZ) = 0;

    // -------------------------------------------------------------------------
    // Resource Barriers
    // -------------------------------------------------------------------------

    /** 对给定纹理插入布局/访问屏障（如 ShaderRead、ColorAttachment 等） */
    virtual void Barrier(const std::vector<TextureHandle>& textures) = 0;

    // -------------------------------------------------------------------------
    // Clear / Viewport / Scissor
    // -------------------------------------------------------------------------

    virtual void ClearColor(TextureHandle texture, const float color[4]) = 0;
    virtual void ClearDepth(TextureHandle texture, float depth,
                           std::uint8_t stencil = 0) = 0;
    virtual void SetViewport(float x, float y, float width, float height,
                            float minDepth = 0.0f, float maxDepth = 1.0f) = 0;
    virtual void SetScissor(std::int32_t x, std::int32_t y, std::uint32_t width,
                            std::uint32_t height) = 0;
};

}  // namespace kale_device
