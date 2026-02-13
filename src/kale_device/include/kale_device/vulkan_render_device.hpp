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

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct VkCommandBuffer_T;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
struct VkFence_T;
typedef struct VkFence_T* VkFence;
struct VkSemaphore_T;
typedef struct VkSemaphore_T* VkSemaphore;
struct VkPipelineLayout_T;
typedef struct VkPipelineLayout_T* VkPipelineLayout;
struct VkDescriptorSetLayout_T;
typedef struct VkDescriptorSetLayout_T* VkDescriptorSetLayout;
struct VkDescriptorSet_T;
typedef struct VkDescriptorSet_T* VkDescriptorSet;
struct VkDescriptorPool_T;
typedef struct VkDescriptorPool_T* VkDescriptorPool;

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
    void CopyBufferToBuffer(BufferHandle srcBuffer, std::size_t srcOffset,
                            BufferHandle dstBuffer, std::size_t dstOffset,
                            std::size_t size) override;
    void CopyBufferToTexture(BufferHandle srcBuffer, std::size_t srcOffset,
                             TextureHandle dstTexture, std::uint32_t mipLevel,
                             std::uint32_t width, std::uint32_t height,
                             std::uint32_t depth = 1) override;
    void CopyTextureToTexture(TextureHandle srcTexture, TextureHandle dstTexture,
                              std::uint32_t width, std::uint32_t height) override;
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
    void WriteDescriptorSetTexture(DescriptorSetHandle set, std::uint32_t binding,
                                    TextureHandle texture) override;
    void WriteDescriptorSetBuffer(DescriptorSetHandle set, std::uint32_t binding,
                                 BufferHandle buffer, std::size_t offset = 0,
                                 std::size_t range = 0) override;

    void DestroyBuffer(BufferHandle handle) override;
    void DestroyTexture(TextureHandle handle) override;
    void DestroyShader(ShaderHandle handle) override;
    void DestroyPipeline(PipelineHandle handle) override;
    void DestroyDescriptorSet(DescriptorSetHandle handle) override;

    DescriptorSetHandle AcquireInstanceDescriptorSet(const void* instanceData,
                                                     std::size_t size) override;
    void ReleaseInstanceDescriptorSet(DescriptorSetHandle handle) override;

    void UpdateBuffer(BufferHandle handle, const void* data, std::size_t size,
                     std::size_t offset = 0) override;
    void UpdateTexture(TextureHandle handle, const void* data,
                       std::uint32_t mipLevel = 0) override;

    void* MapBuffer(BufferHandle handle, std::size_t offset, std::size_t size) override;
    void UnmapBuffer(BufferHandle handle) override;

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
    bool IsFenceSignaled(FenceHandle fence) const override;
    SemaphoreHandle CreateSemaphore() override;

    std::uint32_t AcquireNextImage() override;
    void Present() override;
    TextureHandle GetBackBuffer() override;
    std::uint32_t GetCurrentFrameIndex() const override;

    const DeviceCapabilities& GetCapabilities() const override;

    /// 窗口 resize 时由应用调用，以便下次重建 Swapchain 使用新尺寸
    void SetExtent(std::uint32_t width, std::uint32_t height) override;

    /// 仅供内部/测试：获取底层 Vulkan 上下文
    VulkanContext* GetContext() { return &context_; }
    const VulkanContext* GetContext() const { return &context_; }

private:
    friend class VulkanCommandList;
    bool CreateVmaOrAllocBuffer(const BufferDesc& desc, const void* data,
                                VkBuffer* outBuffer, VkDeviceMemory* outMemory, VkDeviceSize* outSize,
                                void** outVmaAllocation = nullptr);
    void DestroyVmaOrAllocBuffer(VkBuffer buffer, VkDeviceMemory memory);
    bool CreateTextureInternal(const TextureDesc& desc, const void* data,
                              VkImage* outImage, VkDeviceMemory* outMemory, VkImageView* outView,
                              void** outVmaAllocation = nullptr);
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props);
    /** 深度仅 Pass：按 format 缓存 VkRenderPass，供 Shadow Pass 等使用 */
    VkRenderPass GetOrCreateDepthOnlyRenderPass(VkFormat depthFormat);
    /** 深度仅 Pass：按纹理句柄缓存 VkFramebuffer */
    VkFramebuffer GetOrCreateDepthFramebuffer(TextureHandle depthTex);
    bool CreateUploadCommandPoolAndBuffer();
    void DestroyUploadCommandPoolAndBuffer();
    bool CreateFrameSyncObjects();
    void DestroyFrameSyncObjects();
    bool CreateDefaultSampler();
    void DestroyDefaultSampler();
    bool CreateCommandPoolsAndBuffers();
    void DestroyCommandPoolsAndBuffers();

    VulkanContext context_;
    DeviceCapabilities capabilities_{};
    std::uint32_t currentImageIndex_ = 0;
    std::uint32_t currentFrameIndex_ = 0;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t maxRecordingThreads_ = 1;
    static constexpr std::uint32_t kMaxFramesInFlight = 3;

    // 资源表（Phase 2.4）
    std::unordered_map<std::uint64_t, VulkanBufferRes> buffers_;
    std::unordered_map<std::uint64_t, VulkanTextureRes> textures_;
    std::unordered_map<std::uint64_t, VulkanShaderRes> shaders_;
    std::unordered_map<std::uint64_t, VulkanPipelineRes> pipelines_;
    std::unordered_map<std::uint64_t, VulkanDescriptorSetRes> descriptorSets_;
    VkSampler defaultSampler_ = VK_NULL_HANDLE;  // 材质纹理 WriteDescriptorSetTexture 用
    std::map<VkFormat, VkRenderPass> depthOnlyRenderPasses_;
    std::unordered_map<std::uint64_t, VkFramebuffer> depthFramebuffers_;
    std::uint64_t nextBufferId_ = 1;
    std::uint64_t nextTextureId_ = 1;
    std::uint64_t nextShaderId_ = 1;
    std::uint64_t nextPipelineId_ = 1;
    std::uint64_t nextDescriptorSetId_ = 1;

    // Phase 9.2: 实例级 DescriptorSet 池（按 layout 分组，此处为单 layout：单 UBO binding）
    VkDescriptorSetLayout instanceDescriptorSetLayout_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> instanceDescriptorPools_;
    struct InstancePoolFreeEntry {
        std::uint64_t id = 0;
        VkDescriptorSet set = VK_NULL_HANDLE;
        BufferHandle bufferHandle{};
        VkDescriptorPool pool = VK_NULL_HANDLE;
    };
    std::vector<InstancePoolFreeEntry> instancePoolFreeList_;
    std::uint64_t nextInstanceDescriptorSetId_ = 1;  // 仅用于从池新分配时的 id
    std::unordered_set<std::uint64_t> instancePoolIds_;   // 属于实例池的 set id，Shutdown 时只 erase 不单独 destroy
    std::unordered_set<std::uint64_t> instancePoolBufferIds_;  // 实例池创建的 buffer id，Shutdown 时统一销毁
    std::unordered_map<std::uint64_t, BufferHandle> instanceSetIdToBuffer_;  // 实例 set id -> 对应 UBO buffer，Release 时归还池
    bool CreateInstancePoolLayoutAndPool();
    void DestroyInstancePoolResources();

    // VMA（phase13-13.5）：不暴露 VMA 头文件，用 void* 存储
    void* vmaAllocator_ = nullptr;
    std::unordered_map<std::uint64_t, void*> bufferAllocations_;
    std::unordered_map<std::uint64_t, void*> textureAllocations_;

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
