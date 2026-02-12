/**
 * @file vulkan_render_device.hpp
 * @brief Vulkan 后端 IRenderDevice 实现
 *
 * Phase 2.4: 资源创建/销毁/更新（Buffer、Texture、Shader、Pipeline、DescriptorSet）
 * Phase 2.5/2.6: 命令与同步、Swapchain 由后续实现。
 */

#pragma once

#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/vulkan_context.hpp>
#include <kale_device/vulkan_rdi_utils.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace kale_device {

/** Vulkan 后端渲染设备 */
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
    bool CreateVmaOrAllocBuffer(const BufferDesc& desc, const void* data,
                                VkBuffer* outBuffer, VkDeviceMemory* outMemory, VkDeviceSize* outSize);
    void DestroyVmaOrAllocBuffer(VkBuffer buffer, VkDeviceMemory memory);
    bool CreateTextureInternal(const TextureDesc& desc, const void* data,
                              VkImage* outImage, VkDeviceMemory* outMemory, VkImageView* outView);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props);
    bool CreateUploadCommandPoolAndBuffer();
    void DestroyUploadCommandPoolAndBuffer();

    VulkanContext context_;
    DeviceCapabilities capabilities_{};
    std::uint32_t currentImageIndex_ = 0;
    std::uint32_t currentFrameIndex_ = 0;

    // 资源表（Phase 2.4）
    std::unordered_map<std::uint64_t, VulkanBufferRes> buffers_;
    std::unordered_map<std::uint64_t, VulkanTextureRes> textures_;
    std::unordered_map<std::uint64_t, VulkanShaderRes> shaders_;
    std::unordered_map<std::uint64_t, VulkanPipelineRes> pipelines_;
    std::unordered_map<std::uint64_t, VulkanDescriptorSetRes> descriptorSets_;
    std::uint64_t nextBufferId_ = 1;
    std::uint64_t nextTextureId_ = 1;
    std::uint64_t nextShaderId_ = 1;
    std::uint64_t nextPipelineId_ = 1;
    std::uint64_t nextDescriptorSetId_ = 1;

    // 上传用（UpdateBuffer/UpdateTexture 的 staging 与 copy 命令）
    VkCommandPool uploadCommandPool_ = nullptr;
    VkCommandBuffer uploadCommandBuffer_ = nullptr;
};

}  // namespace kale_device
