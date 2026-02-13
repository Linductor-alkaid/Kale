/**
 * @file opengl_render_device.hpp
 * @brief OpenGL 后端 IRenderDevice 实现（phase11-11.6）
 *
 * 使用 SDL_GL_CreateContext 创建 GL 上下文；Swapchain 由窗口系统隐式提供；
 * OpenGLCommandList 将 CommandList 调用序列化为命令队列，Submit 时按序执行。
 */

#pragma once

#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace kale_device {

/** OpenGL 实现的 CommandList：录制为命令队列，Submit 时执行 */
class OpenGLCommandList : public CommandList {
public:
    explicit OpenGLCommandList(class OpenGLRenderDevice* device);
    ~OpenGLCommandList() override;

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

    /** 执行录制的命令（由 OpenGLRenderDevice::Submit 调用） */
    void Execute();

private:
    friend class OpenGLRenderDevice;
    void Push(std::function<void()> cmd);

    class OpenGLRenderDevice* device_ = nullptr;
    std::vector<std::function<void()>> commands_;
};

/** OpenGL 后端渲染设备 */
class OpenGLRenderDevice : public IRenderDevice {
public:
    OpenGLRenderDevice() = default;
    ~OpenGLRenderDevice() override;

    OpenGLRenderDevice(const OpenGLRenderDevice&) = delete;
    OpenGLRenderDevice& operator=(const OpenGLRenderDevice&) = delete;

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

    void UpdateBuffer(BufferHandle handle, const void* data, std::size_t size,
                      std::size_t offset = 0) override;
    void* MapBuffer(BufferHandle handle, std::size_t offset, std::size_t size) override;
    void UnmapBuffer(BufferHandle handle) override;
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
    bool IsFenceSignaled(FenceHandle fence) const override;
    SemaphoreHandle CreateSemaphore() override;

    std::uint32_t AcquireNextImage() override;
    void Present() override;
    TextureHandle GetBackBuffer() override;
    std::uint32_t GetCurrentFrameIndex() const override;
    void SetExtent(std::uint32_t width, std::uint32_t height) override;

    const DeviceCapabilities& GetCapabilities() const override;

    // 内部：供 OpenGLCommandList 与资源映射使用
    std::uint64_t NextId() { return nextId_++; }
    static constexpr std::uint64_t kBackBufferTextureId = 1u;

    struct BufferRes { unsigned int glBuffer = 0; std::size_t size = 0; bool cpuVisible = false; };
    struct TextureRes { unsigned int glTexture = 0; std::uint32_t width = 0; std::uint32_t height = 0; };
    struct ShaderRes { unsigned int glShader = 0; ShaderStage stage = ShaderStage::Vertex; };
    struct PipelineRes { unsigned int glProgram = 0; };
    struct DescriptorSetRes {
        std::map<std::uint32_t, TextureHandle> textures;
        std::map<std::uint32_t, std::pair<BufferHandle, std::size_t>> buffers;
    };
    struct FenceRes { void* glSync = nullptr; };

    std::unordered_map<std::uint64_t, BufferRes> buffers_;
    std::unordered_map<std::uint64_t, TextureRes> textures_;
    std::unordered_map<std::uint64_t, ShaderRes> shaders_;
    std::unordered_map<std::uint64_t, PipelineRes> pipelines_;
    std::unordered_map<std::uint64_t, DescriptorSetRes> descriptorSets_;
    std::unordered_map<std::uint64_t, FenceRes> fences_;

    /** 供 OpenGLCommandList::Execute 使用 */
    void EnsureContext();

private:
    bool MakeCurrent();

    void* window_ = nullptr;
    void* glContext_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t currentFrameIndex_ = 0;
    std::uint64_t nextId_ = 2;  // 1 reserved for back buffer
    std::string lastError_;
    DeviceCapabilities capabilities_;
    OpenGLCommandList* activeCommandList_ = nullptr;
};

}  // namespace kale_device
