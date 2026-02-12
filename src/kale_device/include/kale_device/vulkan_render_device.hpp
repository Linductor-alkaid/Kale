/**
 * @file vulkan_render_device.hpp
 * @brief Vulkan 后端 IRenderDevice 实现
 *
 * Phase 2.4: 资源创建/销毁/更新（Buffer、Texture、Shader、Pipeline、DescriptorSet）
 * Phase 2.5: 命令与同步（VulkanCommandList、Fence、Semaphore、Submit）
 * Phase 2.6: Swapchain 与呈现
 */

#pragma once

#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>
#include <kale_device/vulkan_context.hpp>
#include <kale_device/vulkan_rdi_utils.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct VkCommandBuffer_T;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
struct VkFence_T;
typedef struct VkFence_T* VkFence;
struct VkSemaphore_T;
typedef struct VkSemaphore_T* VkSemaphore;
struct VkPipelineLayout_T;
typedef struct VkPipelineLayout_T* VkPipelineLayout;

namespace kale_device {

/** Vulkan 实现的 CommandList，封装 VkCommandBuffer */
class VulkanCommandList : public CommandList {
public:
    friend class VulkanRenderDevice;
    VulkanCommandList(class VulkanRenderDevice* device, VkCommandBuffer buffer,
                      std::uint32_t swapchainImageIndex);
    ~VulkanCommandList() override = default;

    VkCommandBuffer GetCommandBuffer() const { return commandBuffer_; }
    void SetSwapchainImageIndex(std::uint32_t idx) { swapchainImageIndex_ = idx; }

    void BeginRenderPass(const std::vector<TextureHandle>& colorAttachments,
                        TextureHandle depthAttachment = {}) override;
    void EndRenderPass() override;
    void BindPipeline(PipelineHandle pipeline) override;
    void BindDescriptorSet(std::uint32_t set, DescriptorSetHandle descriptorSet) override;
    void BindVertexBuffer(std::uint32_t binding, BufferHandle buffer,
                          std::size_t offset = 0) override;
    void BindIndexBuffer(BufferHandle buffer, std::size_t offset = 0,
                        bool is16Bit = false) override;
    void SetPushConstants(const void* data, std::size_t size,
                         std::size_t offset = 0) override;
    void Draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1,
             std::uint32_t firstVertex = 0, std::uint32_t firstInstance = 0) override;
    void DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1,
                    std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0,
                    std::uint32_t firstInstance = 0) override;
    void Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                  std::uint32_t groupCountZ) override;
    void Barrier(const std::vector<TextureHandle>& textures) override;
    void ClearColor(TextureHandle texture, const float color[4]) override;
    void ClearDepth(TextureHandle texture, float depth,
                    std::uint8_t stencil = 0) override;
    void SetViewport(float x, float y, float width, float height,
                    float minDepth = 0.0f, float maxDepth = 1.0f) override;
    void SetScissor(std::int32_t x, std::int32_t y, std::uint32_t width,
                   std::uint32_t height) override;

private:
    class VulkanRenderDevice* device_ = nullptr;
    VkCommandBuffer commandBuffer_ = nullptr;
    std::uint32_t swapchainImageIndex_ = 0;
    VkPipelineLayout currentPipelineLayout_ = nullptr;
};

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

    /// 窗口 resize 时由应用调用，以便下次重建 Swapchain 使用新尺寸
    void SetExtent(std::uint32_t width, std::uint32_t height);

    /// 仅供内部/测试：获取底层 Vulkan 上下文
    VulkanContext* GetContext() { return &context_; }
    const VulkanContext* GetContext() const { return &context_; }

private:
    friend class VulkanCommandList;
    bool CreateVmaOrAllocBuffer(const BufferDesc& desc, const void* data,
                                VkBuffer* outBuffer, VkDeviceMemory* outMemory, VkDeviceSize* outSize);
    void DestroyVmaOrAllocBuffer(VkBuffer buffer, VkDeviceMemory memory);
    bool CreateTextureInternal(const TextureDesc& desc, const void* data,
                              VkImage* outImage, VkDeviceMemory* outMemory, VkImageView* outView);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props);
    bool CreateUploadCommandPoolAndBuffer();
    void DestroyUploadCommandPoolAndBuffer();
    bool CreateFrameSyncObjects();
    void DestroyFrameSyncObjects();
    bool CreateCommandPoolsAndBuffers();
    void DestroyCommandPoolsAndBuffers();

    VulkanContext context_;
    DeviceCapabilities capabilities_{};
    std::uint32_t currentImageIndex_ = 0;
    std::uint32_t currentFrameIndex_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    static constexpr std::uint32_t kMaxFramesInFlight = 3;

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

    // Phase 2.5: 命令与同步
    std::vector<VkCommandPool> commandPools_;
    std::vector<std::vector<VkCommandBuffer>> commandBuffers_;  // [threadIndex][frameIndex]
    std::vector<std::vector<std::unique_ptr<VulkanCommandList>>> commandListPool_;  // [threadIndex][frameIndex]
    std::vector<VkFence> frameFences_;
    std::vector<VkSemaphore> frameImageAvailableSemaphores_;
    std::vector<VkSemaphore> frameRenderFinishedSemaphores_;
    std::unordered_map<std::uint64_t, VkFence> fences_;
    std::unordered_map<std::uint64_t, VkSemaphore> semaphores_;
    std::uint64_t nextFenceId_ = 1;
    std::uint64_t nextSemaphoreId_ = 1;
};

}  // namespace kale_device
