// Kale 设备抽象层 - Vulkan 基础上下文
// Instance、Physical Device、Logical Device、Surface、Swapchain

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

// 前向声明 Vulkan 类型，避免在头文件中包含 vulkan.h
typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T* VkDevice;
typedef struct VkSurfaceKHR_T* VkSurfaceKHR;
typedef struct VkSwapchainKHR_T* VkSwapchainKHR;
typedef struct VkImage_T* VkImage;
typedef struct VkQueue_T* VkQueue;
typedef struct VkImageView_T* VkImageView;
typedef struct VkRenderPass_T* VkRenderPass;
typedef struct VkFramebuffer_T* VkFramebuffer;
typedef struct VkCommandPool_T* VkCommandPool;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkPipeline_T* VkPipeline;
typedef struct VkPipelineLayout_T* VkPipelineLayout;
typedef struct VkShaderModule_T* VkShaderModule;
typedef struct VkBuffer_T* VkBuffer;
typedef struct VkDeviceMemory_T* VkDeviceMemory;
typedef struct VkSemaphore_T* VkSemaphore;
typedef struct VkFence_T* VkFence;

namespace kale_device {

/// Vulkan 上下文配置（与 DeviceConfig 对齐，Phase 1 简化版）
struct VulkanConfig {
    void* windowHandle = nullptr;   ///< SDL_Window* 转为 void*
    uint32_t width = 0;
    uint32_t height = 0;
    bool enableValidation = false;
    bool vsync = true;
    uint32_t backBufferCount = 3;
};

/// Vulkan 基础上下文：Instance、Device、Surface、Swapchain
/// 不包含 Command Pool/Buffer、Render Pass、Pipeline（由 phase1-1.3 实现）
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    /// 创建 Vulkan Instance、选择 Physical Device、创建 Logical Device、
    /// 使用 SDL_Vulkan_CreateSurface 创建 Surface、创建 Swapchain。
    /// \return 成功返回 true，失败返回 false，可调用 GetLastError()
    bool Initialize(const VulkanConfig& config);

    /// 按创建逆序销毁 Swapchain、Surface、Device、Instance
    void Shutdown();

    /// 初始化失败时的详细错误信息
    const std::string& GetLastError() const { return lastError_; }

    // --- 访问器（仅在 Initialize 成功后有效）---

    VkInstance GetInstance() const { return instance_; }
    VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice_; }
    VkDevice GetDevice() const { return device_; }
    VkSurfaceKHR GetSurface() const { return surface_; }
    VkSwapchainKHR GetSwapchain() const { return swapchain_; }

    /// 当前 Swapchain 图像数量
    uint32_t GetSwapchainImageCount() const {
        return static_cast<uint32_t>(swapchainImages_.size());
    }

    /// 获取 Swapchain 第 index 张图像的 VkImage（0 <= index < GetSwapchainImageCount()）
    VkImage GetSwapchainImage(uint32_t index) const;

    /// Swapchain 图像格式（VkFormat，用于 Render Pass、Framebuffer）
    uint32_t GetSwapchainFormat() const { return swapchainFormat_; }

    // --- 简单三角形渲染（phase1-1.3）---

    /// 创建三角形渲染所需资源：ImageViews、Render Pass、Framebuffers、
    /// Pipeline、Vertex Buffer、Command Pool/Buffers、同步对象。
    /// \param shaderDir 着色器目录（相对可执行文件或 cwd），内含 triangle.vert.spv、triangle.frag.spv
    /// \return 成功返回 true，失败返回 false
    bool CreateTriangleRendering(const char* shaderDir = "shaders");

    /// 销毁三角形渲染资源（在 Shutdown 前调用，或由 Shutdown 内部调用）
    void DestroyTriangleRendering();

    /// 是否已创建三角形渲染（CreateTriangleRendering 成功）
    bool HasTriangleRendering() const { return renderPass_ != nullptr; }

    /// 获取下一帧 Swapchain 图像索引；内部会等待 Fence、Acquire。
    /// \param outImageIndex 输出图像索引
    /// \return 成功返回 true；若需重建 Swapchain（如 VK_ERROR_OUT_OF_DATE_KHR）返回 false
    bool AcquireNextImage(uint32_t& outImageIndex);

    /// 录制当前帧的绘制命令（Clear + Draw 三角形）
    void RecordCommandBuffer(uint32_t imageIndex);

    /// 提交命令并 Present；内部 Signal Fence、推进 currentFrame_。
    /// \return 成功返回 true；若 Present 返回 OUT_OF_DATE 等返回 false
    bool SubmitAndPresent(uint32_t imageIndex);

    /// 图形/呈现队列（当前实现中为同一 Queue Family）
    VkQueue GetGraphicsQueue() const { return graphicsQueue_; }
    VkQueue GetPresentQueue() const { return presentQueue_; }

    /// 图形队列族索引（用于 Command Pool 创建等）
    uint32_t GetGraphicsQueueFamilyIndex() const { return graphicsQueueFamilyIndex_; }

    /// 当前 Swapchain 尺寸（可能与窗口尺寸一致）
    uint32_t GetSwapchainWidth() const { return swapchainWidth_; }
    uint32_t GetSwapchainHeight() const { return swapchainHeight_; }

    /// 是否已成功初始化
    bool IsInitialized() const { return device_ != nullptr; }

private:
    bool CreateInstance(const VulkanConfig& config);
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateSurface(const VulkanConfig& config);
    bool CreateSwapchain(const VulkanConfig& config);

    VkInstance instance_ = nullptr;
    VkPhysicalDevice physicalDevice_ = nullptr;
    VkDevice device_ = nullptr;
    VkSurfaceKHR surface_ = nullptr;
    VkSwapchainKHR swapchain_ = nullptr;
    std::vector<VkImage> swapchainImages_;
    uint32_t swapchainFormat_ = 0;  // VkFormat
    uint32_t swapchainWidth_ = 0;
    uint32_t swapchainHeight_ = 0;

    // 三角形渲染（phase1-1.3）
    std::vector<VkImageView> swapchainImageViews_;
    VkRenderPass renderPass_ = nullptr;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool commandPool_ = nullptr;
    std::vector<VkCommandBuffer> commandBuffers_;
    VkPipelineLayout pipelineLayout_ = nullptr;
    VkPipeline graphicsPipeline_ = nullptr;
    VkShaderModule vertShaderModule_ = nullptr;
    VkShaderModule fragShaderModule_ = nullptr;
    VkBuffer vertexBuffer_ = nullptr;
    VkDeviceMemory vertexBufferMemory_ = nullptr;
    static constexpr uint32_t kMaxFramesInFlight = 2;
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentFrame_ = 0;

    bool CreateTriangleRenderingImpl(const char* shaderDir);
    bool LoadShaderModule(const char* path, VkShaderModule* outModule);
    bool CreateImageViews();
    bool CreateRenderPass();
    bool CreateFramebuffers();
    bool CreateGraphicsPipeline(const char* shaderDir);
    bool CreateVertexBuffer();
    bool CreateCommandPoolAndBuffers();
    bool CreateSyncObjects();

    VkQueue graphicsQueue_ = nullptr;
    VkQueue presentQueue_ = nullptr;
    uint32_t graphicsQueueFamilyIndex_ = 0;

    std::string lastError_;
    bool validationEnabled_ = false;
};

}  // namespace kale_device
