/**
 * @file vulkan_render_device.hpp
 * @brief Vulkan 后端 IRenderDevice 实现（Phase 2.2 最小可初始化实现）
 *
 * 包装 VulkanContext，实现 Initialize/Shutdown/GetLastError；
 * 其余接口为占位，完整实现见 phase2-2.4 / 2.5 / 2.6。
 */

#pragma once

#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/vulkan_context.hpp>

#include <memory>
#include <string>

namespace kale_device {

/** Vulkan 后端渲染设备（当前仅保证可初始化，资源/命令/同步由后续 Phase 实现）*/
class VulkanRenderDevice : public IRenderDevice {
public:
    VulkanRenderDevice() = default;
    ~VulkanRenderDevice() override;

    VulkanRenderDevice(const VulkanRenderDevice&) = delete;
    VulkanRenderDevice& operator=(const VulkanRenderDevice&) = delete;

    bool Initialize(const DeviceConfig& config) override;
    void Shutdown() override;
    const std::string& GetLastError() const override;

    BufferHandle CreateBuffer(const BufferDesc& desc, const void* data = nullptr) override;
    TextureHandle CreateTexture(const TextureDesc& desc, const void* data = nullptr) override;
    ShaderHandle CreateShader(const ShaderDesc& desc) override;
    PipelineHandle CreatePipeline(const PipelineDesc& desc) override;
    DescriptorSetHandle CreateDescriptorSet(const DescriptorSetLayoutDesc& layout) override;

    void DestroyBuffer(BufferHandle handle) override;
    void DestroyTexture(TextureHandle handle) override;
    void DestroyShader(ShaderHandle handle) override;
    void DestroyPipeline(PipelineHandle handle) override;
    void DestroyDescriptorSet(DescriptorSetHandle handle) override;

    void UpdateBuffer(BufferHandle handle, const void* data, std::size_t size,
                     std::size_t offset = 0) override;
    void UpdateTexture(TextureHandle handle, const void* data,
                       std::uint32_t mipLevel = 0) override;

    CommandList* BeginCommandList(std::uint32_t threadIndex = 0) override;
    void EndCommandList(CommandList* cmd) override;
    void Submit(const std::vector<CommandList*>& cmdLists,
                const std::vector<SemaphoreHandle>& waitSemaphores = {},
                const std::vector<SemaphoreHandle>& signalSemaphores = {},
                FenceHandle fence = {}) override;

    void WaitIdle() override;
    FenceHandle CreateFence(bool signaled = false) override;
    void WaitForFence(FenceHandle fence, std::uint64_t timeout = UINT64_MAX) override;
    void ResetFence(FenceHandle fence) override;
    SemaphoreHandle CreateSemaphore() override;

    std::uint32_t AcquireNextImage() override;
    void Present() override;
    TextureHandle GetBackBuffer() override;
    std::uint32_t GetCurrentFrameIndex() const override;

    const DeviceCapabilities& GetCapabilities() const override;

    /// 仅供内部/测试：获取底层 Vulkan 上下文
    VulkanContext* GetContext() { return &context_; }
    const VulkanContext* GetContext() const { return &context_; }

private:
    VulkanContext context_;
    DeviceCapabilities capabilities_{};
    std::uint32_t currentImageIndex_ = 0;
    std::uint32_t currentFrameIndex_ = 0;
};

}  // namespace kale_device
