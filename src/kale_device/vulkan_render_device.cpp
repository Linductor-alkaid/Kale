/**
 * @file vulkan_render_device.cpp
 * @brief VulkanRenderDevice 实现（Phase 2.2 最小可初始化）
 */

#include <kale_device/vulkan_render_device.hpp>
#include <vulkan/vulkan.h>

namespace kale_device {

VulkanRenderDevice::~VulkanRenderDevice() {
    Shutdown();
}

bool VulkanRenderDevice::Initialize(const DeviceConfig& config) {
    VulkanConfig vkConfig;
    vkConfig.windowHandle = config.windowHandle;
    vkConfig.width = config.width;
    vkConfig.height = config.height;
    vkConfig.enableValidation = config.enableValidation;
    vkConfig.vsync = config.vsync;
    vkConfig.backBufferCount = config.backBufferCount;

    if (!context_.Initialize(vkConfig)) {
        return false;
    }

    // 简单填充能力（后续可从 VkPhysicalDevice 查询）
    capabilities_.maxTextureSize = 4096;
    capabilities_.maxComputeWorkGroupSize[0] = 256;
    capabilities_.maxComputeWorkGroupSize[1] = 256;
    capabilities_.maxComputeWorkGroupSize[2] = 64;
    capabilities_.supportsGeometryShader = true;
    capabilities_.supportsTessellation = true;
    capabilities_.supportsComputeShader = true;
    capabilities_.supportsRayTracing = false;

    return true;
}

void VulkanRenderDevice::Shutdown() {
    context_.Shutdown();
    capabilities_ = DeviceCapabilities{};
    currentImageIndex_ = 0;
    currentFrameIndex_ = 0;
}

const std::string& VulkanRenderDevice::GetLastError() const {
    return context_.GetLastError();
}

BufferHandle VulkanRenderDevice::CreateBuffer(const BufferDesc&, const void*) {
    return BufferHandle{};
}

TextureHandle VulkanRenderDevice::CreateTexture(const TextureDesc&, const void*) {
    return TextureHandle{};
}

ShaderHandle VulkanRenderDevice::CreateShader(const ShaderDesc&) {
    return ShaderHandle{};
}

PipelineHandle VulkanRenderDevice::CreatePipeline(const PipelineDesc&) {
    return PipelineHandle{};
}

DescriptorSetHandle VulkanRenderDevice::CreateDescriptorSet(const DescriptorSetLayoutDesc&) {
    return DescriptorSetHandle{};
}

void VulkanRenderDevice::DestroyBuffer(BufferHandle) {}
void VulkanRenderDevice::DestroyTexture(TextureHandle) {}
void VulkanRenderDevice::DestroyShader(ShaderHandle) {}
void VulkanRenderDevice::DestroyPipeline(PipelineHandle) {}
void VulkanRenderDevice::DestroyDescriptorSet(DescriptorSetHandle) {}

void VulkanRenderDevice::UpdateBuffer(BufferHandle, const void*, std::size_t, std::size_t) {}
void VulkanRenderDevice::UpdateTexture(TextureHandle, const void*, std::uint32_t) {}

CommandList* VulkanRenderDevice::BeginCommandList(std::uint32_t) {
    return nullptr;
}

void VulkanRenderDevice::EndCommandList(CommandList*) {}

void VulkanRenderDevice::Submit(const std::vector<CommandList*>&,
                                const std::vector<SemaphoreHandle>&,
                                const std::vector<SemaphoreHandle>&,
                                FenceHandle) {}

void VulkanRenderDevice::WaitIdle() {
    if (context_.IsInitialized()) {
        vkDeviceWaitIdle(context_.GetDevice());
    }
}

FenceHandle VulkanRenderDevice::CreateFence(bool) {
    return FenceHandle{};
}

void VulkanRenderDevice::WaitForFence(FenceHandle, std::uint64_t) {}

void VulkanRenderDevice::ResetFence(FenceHandle) {}

SemaphoreHandle VulkanRenderDevice::CreateSemaphore() {
    return SemaphoreHandle{};
}

std::uint32_t VulkanRenderDevice::AcquireNextImage() {
    if (!context_.IsInitialized()) return 0;
    uint32_t idx = 0;
    if (context_.AcquireNextImage(idx)) {
        currentImageIndex_ = idx;
        return idx;
    }
    return 0;
}

void VulkanRenderDevice::Present() {
    if (context_.IsInitialized() && context_.HasTriangleRendering()) {
        context_.SubmitAndPresent(currentImageIndex_);
    }
    currentFrameIndex_ = (currentFrameIndex_ + 1) % 2;
}

TextureHandle VulkanRenderDevice::GetBackBuffer() {
    TextureHandle h;
    h.id = currentImageIndex_ + 1;  // 非零表示“当前 back buffer”占位
    return h;
}

std::uint32_t VulkanRenderDevice::GetCurrentFrameIndex() const {
    return currentFrameIndex_;
}

const DeviceCapabilities& VulkanRenderDevice::GetCapabilities() const {
    return capabilities_;
}

}  // namespace kale_device
