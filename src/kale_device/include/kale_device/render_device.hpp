/**
 * @file render_device.hpp
 * @brief IRenderDevice 渲染设备抽象接口与工厂
 *
 * 设备抽象层核心：统一资源创建、命令录制、同步与交换链。
 * 与 device_abstraction_layer_design.md 对齐。
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>

namespace kale_device {

// =============================================================================
// 设备配置与能力
// =============================================================================

/** 渲染设备初始化配置 */
struct DeviceConfig {
    void* windowHandle = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool enableValidation = false;
    bool vsync = true;
    std::uint32_t backBufferCount = 3;
    /** 多线程命令录制时预分配的 CommandPool 数量（Vulkan 每线程独立 Pool，至少 1） */
    std::uint32_t maxRecordingThreads = 4;
};

/** 设备能力查询结果 */
struct DeviceCapabilities {
    std::uint32_t maxTextureSize = 0;
    std::uint32_t maxComputeWorkGroupSize[3] = {0, 0, 0};
    bool supportsGeometryShader = false;
    bool supportsTessellation = false;
    bool supportsComputeShader = false;
    bool supportsRayTracing = false;
    /** 多线程命令录制时可用 CommandPool 数量（BeginCommandList(threadIndex) 的 threadIndex 应 < 此值） */
    std::uint32_t maxRecordingThreads = 1;
};

/** 渲染后端枚举 */
enum class Backend {
    Vulkan,
    OpenGL,
};

// =============================================================================
// 渲染设备接口
// =============================================================================

/** 渲染设备抽象接口：资源、命令、同步、交换链 */
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;

    // --- 设备管理 ---
    virtual bool Initialize(const DeviceConfig& config) = 0;
    virtual void Shutdown() = 0;

    /** 初始化失败时的详细错误信息 */
    virtual const std::string& GetLastError() const = 0;

    // --- 资源创建 ---
    virtual BufferHandle CreateBuffer(const BufferDesc& desc, const void* data = nullptr) = 0;
    virtual TextureHandle CreateTexture(const TextureDesc& desc, const void* data = nullptr) = 0;
    virtual ShaderHandle CreateShader(const ShaderDesc& desc) = 0;
    virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;
    virtual DescriptorSetHandle CreateDescriptorSet(const DescriptorSetLayoutDesc& layout) = 0;

    /** 向 DescriptorSet 的 binding 写入纹理（CombinedImageSampler，含默认采样器） */
    virtual void WriteDescriptorSetTexture(DescriptorSetHandle set, std::uint32_t binding,
                                           TextureHandle texture) = 0;

    /** 向 DescriptorSet 的 binding 写入 UniformBuffer（供实例级 UBO 等使用） */
    virtual void WriteDescriptorSetBuffer(DescriptorSetHandle set, std::uint32_t binding,
                                         BufferHandle buffer, std::size_t offset = 0,
                                         std::size_t range = 0) = 0;

    // --- 资源销毁 ---
    virtual void DestroyBuffer(BufferHandle handle) = 0;
    virtual void DestroyTexture(TextureHandle handle) = 0;
    virtual void DestroyShader(ShaderHandle handle) = 0;
    virtual void DestroyPipeline(PipelineHandle handle) = 0;
    virtual void DestroyDescriptorSet(DescriptorSetHandle handle) = 0;

    /**
     * 从设备层实例 DescriptorSet 池取得一个 set，写入 instanceData，供 per-instance UBO 使用。
     * 帧末由上层调用 ReleaseInstanceDescriptorSet 回收到池。
     * 默认返回无效句柄（未实现池的后端）。
     */
    virtual DescriptorSetHandle AcquireInstanceDescriptorSet(const void* instanceData,
                                                            std::size_t size) {
        (void)instanceData;
        (void)size;
        return DescriptorSetHandle{};
    }

    /** 将实例 set 归还设备池，供下一帧复用。默认空实现。 */
    virtual void ReleaseInstanceDescriptorSet(DescriptorSetHandle handle) { (void)handle; }

    // --- 资源更新 ---
    virtual void UpdateBuffer(BufferHandle handle, const void* data, std::size_t size,
                             std::size_t offset = 0) = 0;

    /** 映射 CPU 可见 Buffer 的指定范围，返回可写指针；UnmapBuffer 前勿销毁 Buffer */
    virtual void* MapBuffer(BufferHandle handle, std::size_t offset, std::size_t size) = 0;
    virtual void UnmapBuffer(BufferHandle handle) = 0;
    virtual void UpdateTexture(TextureHandle handle, const void* data,
                              std::uint32_t mipLevel = 0) = 0;

    // --- 命令录制（多线程时每线程独立 CommandPool）---
    virtual CommandList* BeginCommandList(std::uint32_t threadIndex = 0) = 0;
    virtual void EndCommandList(CommandList* cmd) = 0;
    virtual void Submit(const std::vector<CommandList*>& cmdLists,
                        const std::vector<SemaphoreHandle>& waitSemaphores = {},
                        const std::vector<SemaphoreHandle>& signalSemaphores = {},
                        FenceHandle fence = {}) = 0;

    // --- 同步 ---
    virtual void WaitIdle() = 0;
    virtual FenceHandle CreateFence(bool signaled = false) = 0;
    virtual void WaitForFence(FenceHandle fence, std::uint64_t timeout = UINT64_MAX) = 0;
    virtual void ResetFence(FenceHandle fence) = 0;
    /** 非阻塞查询 Fence 是否已 signal（供 Staging 池回收等使用） */
    virtual bool IsFenceSignaled(FenceHandle fence) const = 0;
    virtual SemaphoreHandle CreateSemaphore() = 0;

    // --- 交换链 ---
    /** 获取下一帧 swapchain 图像索引；失败返回 kInvalidSwapchainImageIndex。 */
    virtual std::uint32_t AcquireNextImage() = 0;
    static constexpr std::uint32_t kInvalidSwapchainImageIndex = 0xFFFFFFFFu;
    virtual void Present() = 0;
    virtual TextureHandle GetBackBuffer() = 0;
    virtual std::uint32_t GetCurrentFrameIndex() const = 0;

    /** 窗口 resize 时由引擎调用，以便下次重建 Swapchain 使用新尺寸；默认空实现。 */
    virtual void SetExtent(std::uint32_t width, std::uint32_t height) { (void)width; (void)height; }

    // --- 查询 ---
    virtual const DeviceCapabilities& GetCapabilities() const = 0;
};

// =============================================================================
// 工厂
// =============================================================================

/** 根据后端创建渲染设备实例 */
std::unique_ptr<IRenderDevice> CreateRenderDevice(Backend backend);

}  // namespace kale_device
