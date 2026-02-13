/**
 * @file vulkan_render_device.cpp
 * @brief VulkanRenderDevice 实现（Phase 2.4 资源创建/销毁/更新；Phase 9.2 实例 DescriptorSet 池；Phase 13.5 VMA 集成）
 */

#ifdef KALE_USE_VMA
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#endif

#include <kale_device/rdi_types.hpp>
#include <kale_device/vulkan_render_device.hpp>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace kale_device {

// =============================================================================
// 生命周期
// =============================================================================

VulkanRenderDevice::~VulkanRenderDevice() {
    Shutdown();
}

bool VulkanRenderDevice::Initialize(const DeviceConfig& config) {
    lastError_.clear();
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
    width_ = config.width;
    height_ = config.height;
    maxRecordingThreads_ = (config.maxRecordingThreads > 0u) ? config.maxRecordingThreads : 1u;

    if (!CreateUploadCommandPoolAndBuffer()) {
        lastError_ = "VulkanRenderDevice: CreateUploadCommandPoolAndBuffer failed";
        Shutdown();
        return false;
    }
    if (!CreateFrameSyncObjects() || !CreateCommandPoolsAndBuffers()) {
        lastError_ = "VulkanRenderDevice: CreateFrameSyncObjects or CreateCommandPoolsAndBuffers failed";
        Shutdown();
        return false;
    }
    if (!CreateDefaultSampler()) {
        lastError_ = "VulkanRenderDevice: CreateDefaultSampler failed";
        Shutdown();
        return false;
    }

#ifdef KALE_USE_VMA
    {
        VmaAllocatorCreateInfo vmaInfo = {};
        vmaInfo.instance = context_.GetInstance();
        vmaInfo.physicalDevice = context_.GetPhysicalDevice();
        vmaInfo.device = context_.GetDevice();
        vmaInfo.vulkanApiVersion = VK_API_VERSION_1_0;
        VmaAllocator alloc = nullptr;
        if (vmaCreateAllocator(&vmaInfo, &alloc) != VK_SUCCESS) {
            lastError_ = "VulkanRenderDevice: VMA allocator creation failed";
            Shutdown();
            return false;
        }
        vmaAllocator_ = alloc;
    }
#endif

    // 从 VkPhysicalDevice 查询设备能力（phase11-11.7）
    {
        VkPhysicalDevice physical = context_.GetPhysicalDevice();
        VkPhysicalDeviceProperties props = {};
        VkPhysicalDeviceFeatures features = {};
        vkGetPhysicalDeviceProperties(physical, &props);
        vkGetPhysicalDeviceFeatures(physical, &features);

        capabilities_.maxTextureSize = props.limits.maxImageDimension2D;
        capabilities_.maxComputeWorkGroupSize[0] = props.limits.maxComputeWorkGroupSize[0];
        capabilities_.maxComputeWorkGroupSize[1] = props.limits.maxComputeWorkGroupSize[1];
        capabilities_.maxComputeWorkGroupSize[2] = props.limits.maxComputeWorkGroupSize[2];
        capabilities_.supportsGeometryShader = (features.geometryShader != VK_FALSE);
        capabilities_.supportsTessellation = (features.tessellationShader != VK_FALSE);
        capabilities_.supportsRayTracing = false;  // 未启用 RT 扩展

        std::uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queueFamilyCount, queueFamilies.data());
        bool hasCompute = false;
        for (const auto& qf : queueFamilies) {
            if (qf.queueFlags & VK_QUEUE_COMPUTE_BIT) {
                hasCompute = true;
                break;
            }
        }
        capabilities_.supportsComputeShader = hasCompute;
        capabilities_.maxRecordingThreads = maxRecordingThreads_;
    }

    return true;
}

void VulkanRenderDevice::Shutdown() {
    if (!context_.IsInitialized()) return;

    VkDevice dev = context_.GetDevice();

    // 销毁顺序：先依赖资源的资源，再底层资源（phase13-13.10）
    // 1) 依赖 texture 的 framebuffers 和 render passes
    for (auto& [id, fb] : depthFramebuffers_) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(dev, fb, nullptr);
    }
    depthFramebuffers_.clear();
    for (auto& [fmt, rp] : depthOnlyRenderPasses_) {
        if (rp != VK_NULL_HANDLE) vkDestroyRenderPass(dev, rp, nullptr);
    }
    depthOnlyRenderPasses_.clear();

#ifdef KALE_USE_VMA
    VmaAllocator alloc = static_cast<VmaAllocator>(vmaAllocator_);
    if (alloc) {
        for (auto& [id, res] : buffers_) {
            auto it = bufferAllocations_.find(id);
            if (it != bufferAllocations_.end()) {
                if (res.mappedPtr) vmaUnmapMemory(alloc, static_cast<VmaAllocation>(it->second));
                vmaDestroyBuffer(alloc, res.buffer, static_cast<VmaAllocation>(it->second));
            } else {
                if (res.mappedPtr) vkUnmapMemory(dev, res.memory);
                if (res.buffer != VK_NULL_HANDLE) vkDestroyBuffer(dev, res.buffer, nullptr);
                if (res.memory != VK_NULL_HANDLE) vkFreeMemory(dev, res.memory, nullptr);
            }
        }
        bufferAllocations_.clear();
        buffers_.clear();
        for (auto& [id, res] : textures_) {
            if (res.view != VK_NULL_HANDLE) vkDestroyImageView(dev, res.view, nullptr);
            auto it = textureAllocations_.find(id);
            if (it != textureAllocations_.end())
                vmaDestroyImage(alloc, res.image, static_cast<VmaAllocation>(it->second));
            else {
                if (res.image != VK_NULL_HANDLE) vkDestroyImage(dev, res.image, nullptr);
                if (res.memory != VK_NULL_HANDLE) vkFreeMemory(dev, res.memory, nullptr);
            }
        }
        textureAllocations_.clear();
        textures_.clear();
        vmaDestroyAllocator(alloc);
        vmaAllocator_ = nullptr;
    } else
#endif
    {
    for (auto& [id, res] : buffers_) {
        if (res.mappedPtr) vkUnmapMemory(dev, res.memory);
        if (res.buffer != VK_NULL_HANDLE) vkDestroyBuffer(dev, res.buffer, nullptr);
        if (res.memory != VK_NULL_HANDLE) vkFreeMemory(dev, res.memory, nullptr);
    }
    buffers_.clear();

    for (auto& [id, res] : textures_) {
        if (res.view != VK_NULL_HANDLE) vkDestroyImageView(dev, res.view, nullptr);
        if (res.image != VK_NULL_HANDLE) vkDestroyImage(dev, res.image, nullptr);
        if (res.memory != VK_NULL_HANDLE) vkFreeMemory(dev, res.memory, nullptr);
    }
    textures_.clear();
    }

    for (auto& [id, res] : shaders_) {
        if (res.module != VK_NULL_HANDLE) vkDestroyShaderModule(dev, res.module, nullptr);
    }
    shaders_.clear();

    for (auto& [id, res] : pipelines_) {
        if (res.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, res.pipeline, nullptr);
        if (res.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, res.layout, nullptr);
    }
    pipelines_.clear();

    for (std::uint64_t id : instancePoolIds_)
        descriptorSets_.erase(id);
    DestroyInstancePoolResources();

    for (auto& [id, res] : descriptorSets_) {
        if (res.set != VK_NULL_HANDLE) vkFreeDescriptorSets(dev, res.pool, 1, &res.set);
        if (res.pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(dev, res.pool, nullptr);
        if (res.layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, res.layout, nullptr);
    }
    descriptorSets_.clear();

    DestroyDefaultSampler();
    DestroyCommandPoolsAndBuffers();
    DestroyFrameSyncObjects();
    DestroyUploadCommandPoolAndBuffer();
    context_.Shutdown();
    capabilities_ = DeviceCapabilities{};
    currentImageIndex_ = 0;
    currentFrameIndex_ = 0;
}

const std::string& VulkanRenderDevice::GetLastError() const {
    if (!lastError_.empty()) return lastError_;
    return context_.GetLastError();
}

// =============================================================================
// 内存与 Buffer 辅助
// =============================================================================

uint32_t VulkanRenderDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDevice physical = context_.GetPhysicalDevice();
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physical, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return UINT32_MAX;
}

VkRenderPass VulkanRenderDevice::GetOrCreateDepthOnlyRenderPass(VkFormat depthFormat) {
    if (depthFormat == VK_FORMAT_UNDEFINED) return VK_NULL_HANDLE;
    auto it = depthOnlyRenderPasses_.find(depthFormat);
    if (it != depthOnlyRenderPasses_.end()) return it->second;

    VkDevice dev = context_.GetDevice();
    VkAttachmentDescription depthAttachment = {};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef = {};
    depthRef.attachment = 0;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pColorAttachments = nullptr;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo = {};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &depthAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(dev, &rpInfo, nullptr, &rp) != VK_SUCCESS) return VK_NULL_HANDLE;
    depthOnlyRenderPasses_[depthFormat] = rp;
    return rp;
}

VkFramebuffer VulkanRenderDevice::GetOrCreateDepthFramebuffer(TextureHandle depthTex) {
    if (!depthTex.IsValid()) return VK_NULL_HANDLE;
    auto it = depthFramebuffers_.find(depthTex.id);
    if (it != depthFramebuffers_.end()) return it->second;

    auto texIt = textures_.find(depthTex.id);
    if (texIt == textures_.end()) return VK_NULL_HANDLE;
    const VulkanTextureRes& res = texIt->second;
    if (res.view == VK_NULL_HANDLE || res.image == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    VkFormat format = ToVkFormat(res.desc.format);
    VkRenderPass rp = GetOrCreateDepthOnlyRenderPass(format);
    if (rp == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    VkFramebufferCreateInfo fbInfo = {};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = rp;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &res.view;
    fbInfo.width = res.desc.width;
    fbInfo.height = res.desc.height;
    fbInfo.layers = 1;

    VkFramebuffer fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(context_.GetDevice(), &fbInfo, nullptr, &fb) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    depthFramebuffers_[depthTex.id] = fb;
    return fb;
}

bool VulkanRenderDevice::CreateVmaOrAllocBuffer(const BufferDesc& desc, const void* data,
                                                VkBuffer* outBuffer, VkDeviceMemory* outMemory,
                                                VkDeviceSize* outSize, void** outVmaAllocation) {
    VkDevice dev = context_.GetDevice();
    VkDeviceSize size = desc.size;
    if (size == 0) return false;

    VkBufferUsageFlags usage = ToVkBufferUsage(desc.usage);
    /* 非 cpuVisible 时允许 Staging 上传（data 为空则由 ModelLoader 等通过 Staging 上传） */
    if (!desc.cpuVisible)
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkBufferCreateInfo bufInfo = {};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

#ifdef KALE_USE_VMA
    if (vmaAllocator_ && outVmaAllocation) {
        VmaAllocator alloc = static_cast<VmaAllocator>(vmaAllocator_);
        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = desc.cpuVisible ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;
        if (desc.cpuVisible)
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocation allocation = nullptr;
        VkResult err = vmaCreateBuffer(alloc, &bufInfo, &allocCreateInfo, outBuffer, &allocation, nullptr);
        if (err != VK_SUCCESS) return false;
        *outVmaAllocation = allocation;
        *outMemory = VK_NULL_HANDLE;
        *outSize = size;
        if (data && desc.cpuVisible) {
            void* mapped = nullptr;
            if (vmaMapMemory(alloc, allocation, &mapped) == VK_SUCCESS && mapped)
                memcpy(mapped, data, size);
        } else if (data && !desc.cpuVisible) {
            VmaAllocationCreateInfo stagingInfo = {};
            stagingInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            stagingInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VkBuffer stagingBuf = VK_NULL_HANDLE;
            VmaAllocation stagingAlloc = nullptr;
            VkBufferCreateInfo stagingBufInfo = bufInfo;
            stagingBufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            if (vmaCreateBuffer(alloc, &stagingBufInfo, &stagingInfo, &stagingBuf, &stagingAlloc, nullptr) != VK_SUCCESS) {
                vmaDestroyBuffer(alloc, *outBuffer, allocation);
                *outBuffer = VK_NULL_HANDLE;
                *outVmaAllocation = nullptr;
                return false;
            }
            void* mapped = nullptr;
            vmaMapMemory(alloc, stagingAlloc, &mapped);
            if (mapped) memcpy(mapped, data, size);
            vmaUnmapMemory(alloc, stagingAlloc);
            vkResetCommandBuffer(uploadCommandBuffer_, 0);
            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            vkBeginCommandBuffer(uploadCommandBuffer_, &beginInfo);
            VkBufferCopy copy = {};
            copy.size = size;
            vkCmdCopyBuffer(uploadCommandBuffer_, stagingBuf, *outBuffer, 1, &copy);
            vkEndCommandBuffer(uploadCommandBuffer_);
            VkSubmitInfo submit = {};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &uploadCommandBuffer_;
            vkQueueSubmit(context_.GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
            vkQueueWaitIdle(context_.GetGraphicsQueue());
            vmaDestroyBuffer(alloc, stagingBuf, stagingAlloc);
        }
        return true;
    }
#endif

    VkResult err = vkCreateBuffer(dev, &bufInfo, nullptr, outBuffer);
    if (err != VK_SUCCESS) return false;

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(dev, *outBuffer, &memReqs);

    VkMemoryPropertyFlags wantProps = desc.cpuVisible
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    uint32_t memTypeIndex = FindMemoryType(memReqs.memoryTypeBits, wantProps);
    if (memTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(dev, *outBuffer, nullptr);
        *outBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memTypeIndex;

    err = vkAllocateMemory(dev, &allocInfo, nullptr, outMemory);
    if (err != VK_SUCCESS) {
        vkDestroyBuffer(dev, *outBuffer, nullptr);
        *outBuffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(dev, *outBuffer, *outMemory, 0);
    *outSize = size;

    if (data && desc.cpuVisible) {
        /* cpuVisible 时由 CreateBuffer 内统一做持久映射并拷贝 */
    } else if (data && !desc.cpuVisible) {
        // Staging buffer upload
        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        VkBufferCreateInfo stagingInfo = {};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = size;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(dev, &stagingInfo, nullptr, &stagingBuf) != VK_SUCCESS) {
            vkFreeMemory(dev, *outMemory, nullptr);
            vkDestroyBuffer(dev, *outBuffer, nullptr);
            *outBuffer = VK_NULL_HANDLE;
            *outMemory = VK_NULL_HANDLE;
            return false;
        }
        VkMemoryRequirements stagingReqs;
        vkGetBufferMemoryRequirements(dev, stagingBuf, &stagingReqs);
        uint32_t stagingType = FindMemoryType(stagingReqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (stagingType == UINT32_MAX) {
            vkDestroyBuffer(dev, stagingBuf, nullptr);
            vkFreeMemory(dev, *outMemory, nullptr);
            vkDestroyBuffer(dev, *outBuffer, nullptr);
            *outBuffer = VK_NULL_HANDLE;
            *outMemory = VK_NULL_HANDLE;
            return false;
        }
        VkMemoryAllocateInfo stagingAlloc = {};
        stagingAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        stagingAlloc.allocationSize = stagingReqs.size;
        stagingAlloc.memoryTypeIndex = stagingType;
        if (vkAllocateMemory(dev, &stagingAlloc, nullptr, &stagingMem) != VK_SUCCESS) {
            vkDestroyBuffer(dev, stagingBuf, nullptr);
            vkFreeMemory(dev, *outMemory, nullptr);
            vkDestroyBuffer(dev, *outBuffer, nullptr);
            *outBuffer = VK_NULL_HANDLE;
            *outMemory = VK_NULL_HANDLE;
            return false;
        }
        vkBindBufferMemory(dev, stagingBuf, stagingMem, 0);
        void* mapped = nullptr;
        vkMapMemory(dev, stagingMem, 0, size, 0, &mapped);
        if (mapped) memcpy(mapped, data, size);
        vkUnmapMemory(dev, stagingMem);

        vkResetCommandBuffer(uploadCommandBuffer_, 0);
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(uploadCommandBuffer_, &beginInfo);
        VkBufferCopy copy = {};
        copy.size = size;
        vkCmdCopyBuffer(uploadCommandBuffer_, stagingBuf, *outBuffer, 1, &copy);
        vkEndCommandBuffer(uploadCommandBuffer_);

        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &uploadCommandBuffer_;
        vkQueueSubmit(context_.GetGraphicsQueue(), 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(context_.GetGraphicsQueue());

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
    }
    return true;
}

void VulkanRenderDevice::DestroyVmaOrAllocBuffer(VkBuffer buffer, VkDeviceMemory memory) {
    VkDevice dev = context_.GetDevice();
    if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(dev, buffer, nullptr);
    if (memory != VK_NULL_HANDLE) vkFreeMemory(dev, memory, nullptr);
}

// =============================================================================
// CreateBuffer / CreateTexture
// =============================================================================

BufferHandle VulkanRenderDevice::CreateBuffer(const BufferDesc& desc, const void* data) {
    if (!context_.IsInitialized()) return BufferHandle{};
    if (desc.size == 0) return BufferHandle{};

    void* vmaAlloc = nullptr;
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    if (!CreateVmaOrAllocBuffer(desc, data, &buf, &mem, &size, &vmaAlloc)) {
        return BufferHandle{};
    }

    std::uint64_t id = nextBufferId_++;
    void* mappedPtr = nullptr;
#ifdef KALE_USE_VMA
    if (vmaAlloc && desc.cpuVisible) {
        VmaAllocationInfo allocInfo = {};
        vmaGetAllocationInfo(static_cast<VmaAllocator>(vmaAllocator_), static_cast<VmaAllocation>(vmaAlloc), &allocInfo);
        mappedPtr = allocInfo.pMappedData;
    }
#endif
    if (!mappedPtr && desc.cpuVisible && mem != VK_NULL_HANDLE) {
        VkDevice dev = context_.GetDevice();
        void* mapped = nullptr;
        if (vkMapMemory(dev, mem, 0, size, 0, &mapped) == VK_SUCCESS) {
            mappedPtr = mapped;
            if (data) memcpy(mapped, data, size);
        }
    }
    buffers_[id] = VulkanBufferRes{ buf, mem, size, desc.cpuVisible, mappedPtr };
    if (vmaAlloc) bufferAllocations_[id] = vmaAlloc;
    BufferHandle h;
    h.id = id;
    return h;
}

bool VulkanRenderDevice::CreateTextureInternal(const TextureDesc& desc, const void* data,
                                               VkImage* outImage, VkDeviceMemory* outMemory,
                                               VkImageView* outView, void** outVmaAllocation) {
    VkDevice dev = context_.GetDevice();
    VkFormat format = ToVkFormat(desc.format);
    if (format == VK_FORMAT_UNDEFINED) return false;

    VkImageUsageFlags usage = ToVkImageUsage(desc.usage);
    /* 允许无初始数据时通过 Staging 上传（TextureLoader 集成 Staging） */
    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkImageCreateInfo imageInfo = {};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { desc.width, desc.height, desc.depth };
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (desc.isCube) imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

#ifdef KALE_USE_VMA
    if (vmaAllocator_ && outVmaAllocation) {
        VmaAllocator alloc = static_cast<VmaAllocator>(vmaAllocator_);
        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        VmaAllocation allocation = nullptr;
        VkResult err = vmaCreateImage(alloc, &imageInfo, &allocCreateInfo, outImage, &allocation, nullptr);
        if (err != VK_SUCCESS) return false;
        *outVmaAllocation = allocation;
        *outMemory = VK_NULL_HANDLE;
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = *outImage;
        viewInfo.viewType = desc.isCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT)
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        else if (format >= VK_FORMAT_D16_UNORM && format <= VK_FORMAT_D32_SFLOAT)
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.aspectMask = aspectMask;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = desc.mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = desc.arrayLayers;
        err = vkCreateImageView(dev, &viewInfo, nullptr, outView);
        if (err != VK_SUCCESS) {
            vmaDestroyImage(alloc, *outImage, allocation);
            *outImage = VK_NULL_HANDLE;
            *outVmaAllocation = nullptr;
            return false;
        }
        if (data && aspectMask == VK_IMAGE_ASPECT_COLOR_BIT) {
            size_t pixelSize = 4;
            if (format == VK_FORMAT_R32G32B32A32_SFLOAT) pixelSize = 16;
            size_t totalSize = static_cast<size_t>(desc.width) * desc.height * desc.arrayLayers * pixelSize;
            VmaAllocationCreateInfo stagingInfo = {};
            stagingInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            stagingInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VkBufferCreateInfo bufInfo = {};
            bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufInfo.size = totalSize;
            bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VkBuffer stagingBuf = VK_NULL_HANDLE;
            VmaAllocation stagingAlloc = nullptr;
            if (vmaCreateBuffer(alloc, &bufInfo, &stagingInfo, &stagingBuf, &stagingAlloc, nullptr) != VK_SUCCESS) {
                vkDestroyImageView(dev, *outView, nullptr);
                vmaDestroyImage(alloc, *outImage, allocation);
                *outImage = VK_NULL_HANDLE;
                *outView = VK_NULL_HANDLE;
                *outVmaAllocation = nullptr;
                return false;
            }
            void* mapped = nullptr;
            vmaMapMemory(alloc, stagingAlloc, &mapped);
            if (mapped) memcpy(mapped, data, totalSize);
            vmaUnmapMemory(alloc, stagingAlloc);
            vkResetCommandBuffer(uploadCommandBuffer_, 0);
            VkCommandBufferBeginInfo bi = {};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            vkBeginCommandBuffer(uploadCommandBuffer_, &bi);
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.image = *outImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = desc.mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = desc.arrayLayers;
            vkCmdPipelineBarrier(uploadCommandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy region = {};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = desc.arrayLayers;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { desc.width, desc.height, desc.depth };
            vkCmdCopyBufferToImage(uploadCommandBuffer_, stagingBuf, *outImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(uploadCommandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
            vkEndCommandBuffer(uploadCommandBuffer_);
            VkSubmitInfo si = {};
            si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            si.commandBufferCount = 1;
            si.pCommandBuffers = &uploadCommandBuffer_;
            vkQueueSubmit(context_.GetGraphicsQueue(), 1, &si, VK_NULL_HANDLE);
            vkQueueWaitIdle(context_.GetGraphicsQueue());
            vmaDestroyBuffer(alloc, stagingBuf, stagingAlloc);
        }
        return true;
    }
#endif

    VkResult err = vkCreateImage(dev, &imageInfo, nullptr, outImage);
    if (err != VK_SUCCESS) return false;

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(dev, *outImage, &memReqs);
    uint32_t memType = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memType == UINT32_MAX) {
        vkDestroyImage(dev, *outImage, nullptr);
        *outImage = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = memType;
    err = vkAllocateMemory(dev, &allocInfo, nullptr, outMemory);
    if (err != VK_SUCCESS) {
        vkDestroyImage(dev, *outImage, nullptr);
        *outImage = VK_NULL_HANDLE;
        return false;
    }
    vkBindImageMemory(dev, *outImage, *outMemory, 0);

    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = *outImage;
    viewInfo.viewType = desc.isCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT)
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    else if (format >= VK_FORMAT_D16_UNORM && format <= VK_FORMAT_D32_SFLOAT)
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc.arrayLayers;
    err = vkCreateImageView(dev, &viewInfo, nullptr, outView);
    if (err != VK_SUCCESS) {
        vkFreeMemory(dev, *outMemory, nullptr);
        vkDestroyImage(dev, *outImage, nullptr);
        *outImage = VK_NULL_HANDLE;
        *outMemory = VK_NULL_HANDLE;
        return false;
    }

    if (data && aspectMask == VK_IMAGE_ASPECT_COLOR_BIT) {
        // Staging buffer -> image copy (color only; depth textures skip initial upload here)
        size_t pixelSize = 4;  // RGBA8
        if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB) pixelSize = 4;
        else if (format == VK_FORMAT_R32G32B32A32_SFLOAT) pixelSize = 16;
        size_t layerSize = static_cast<size_t>(desc.width) * desc.height * pixelSize;
        size_t totalSize = layerSize * desc.arrayLayers * desc.mipLevels;
        // 简化：只上传 mip0
        totalSize = static_cast<size_t>(desc.width) * desc.height * desc.arrayLayers * pixelSize;

        VkBuffer stagingBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        VkBufferCreateInfo stagingInfo = {};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = totalSize;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(dev, &stagingInfo, nullptr, &stagingBuf) != VK_SUCCESS) {
            vkDestroyImageView(dev, *outView, nullptr);
            vkFreeMemory(dev, *outMemory, nullptr);
            vkDestroyImage(dev, *outImage, nullptr);
            *outImage = VK_NULL_HANDLE;
            *outMemory = VK_NULL_HANDLE;
            *outView = VK_NULL_HANDLE;
            return false;
        }
        VkMemoryRequirements sr;
        vkGetBufferMemoryRequirements(dev, stagingBuf, &sr);
        uint32_t st = FindMemoryType(sr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (st == UINT32_MAX) {
            vkDestroyBuffer(dev, stagingBuf, nullptr);
            vkDestroyImageView(dev, *outView, nullptr);
            vkFreeMemory(dev, *outMemory, nullptr);
            vkDestroyImage(dev, *outImage, nullptr);
            *outImage = VK_NULL_HANDLE;
            *outMemory = VK_NULL_HANDLE;
            *outView = VK_NULL_HANDLE;
            return false;
        }
        VkMemoryAllocateInfo ma = {};
        ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ma.allocationSize = sr.size;
        ma.memoryTypeIndex = st;
        if (vkAllocateMemory(dev, &ma, nullptr, &stagingMem) != VK_SUCCESS) {
            vkDestroyBuffer(dev, stagingBuf, nullptr);
            vkDestroyImageView(dev, *outView, nullptr);
            vkFreeMemory(dev, *outMemory, nullptr);
            vkDestroyImage(dev, *outImage, nullptr);
            *outImage = VK_NULL_HANDLE;
            *outMemory = VK_NULL_HANDLE;
            *outView = VK_NULL_HANDLE;
            return false;
        }
        vkBindBufferMemory(dev, stagingBuf, stagingMem, 0);
        void* mapped = nullptr;
        vkMapMemory(dev, stagingMem, 0, totalSize, 0, &mapped);
        if (mapped) memcpy(mapped, data, totalSize);
        vkUnmapMemory(dev, stagingMem);

        vkResetCommandBuffer(uploadCommandBuffer_, 0);
        VkCommandBufferBeginInfo bi = {};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(uploadCommandBuffer_, &bi);

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.image = *outImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = desc.mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = desc.arrayLayers;
        vkCmdPipelineBarrier(uploadCommandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = desc.arrayLayers;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { desc.width, desc.height, desc.depth };
        vkCmdCopyBufferToImage(uploadCommandBuffer_, stagingBuf, *outImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(uploadCommandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkEndCommandBuffer(uploadCommandBuffer_);
        VkSubmitInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &uploadCommandBuffer_;
        vkQueueSubmit(context_.GetGraphicsQueue(), 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(context_.GetGraphicsQueue());

        vkDestroyBuffer(dev, stagingBuf, nullptr);
        vkFreeMemory(dev, stagingMem, nullptr);
    }
    return true;
}

TextureHandle VulkanRenderDevice::CreateTexture(const TextureDesc& desc, const void* data) {
    if (!context_.IsInitialized()) return TextureHandle{};
    if (desc.width == 0 || desc.height == 0) return TextureHandle{};

    void* texVmaAlloc = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    if (!CreateTextureInternal(desc, data, &image, &memory, &view, &texVmaAlloc)) {
        return TextureHandle{};
    }

    std::uint64_t id = nextTextureId_++;
    VulkanTextureRes res;
    res.image = image;
    res.memory = memory;
    res.view = view;
    res.desc = desc;
    textures_[id] = std::move(res);
    if (texVmaAlloc) textureAllocations_[id] = texVmaAlloc;
    TextureHandle h;
    h.id = id;
    return h;
}

// =============================================================================
// CreateShader / CreatePipeline / CreateDescriptorSet
// =============================================================================

ShaderHandle VulkanRenderDevice::CreateShader(const ShaderDesc& desc) {
    if (!context_.IsInitialized()) return ShaderHandle{};
    if (desc.code.empty()) return ShaderHandle{};

    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = desc.code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(desc.code.data());

    VkShaderModule mod = VK_NULL_HANDLE;
    VkResult err = vkCreateShaderModule(context_.GetDevice(), &createInfo, nullptr, &mod);
    if (err != VK_SUCCESS) return ShaderHandle{};

    std::uint64_t id = nextShaderId_++;
    shaders_[id] = VulkanShaderRes{ mod, desc.stage };
    ShaderHandle h;
    h.id = id;
    return h;
}

PipelineHandle VulkanRenderDevice::CreatePipeline(const PipelineDesc& desc) {
    if (!context_.IsInitialized()) return PipelineHandle{};
    if (desc.shaders.empty()) return PipelineHandle{};

    VkDevice dev = context_.GetDevice();

    std::vector<VkPipelineShaderStageCreateInfo> stages;
    for (const auto& sh : desc.shaders) {
        auto it = shaders_.find(sh.id);
        if (it == shaders_.end()) continue;
        VkPipelineShaderStageCreateInfo si = {};
        si.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        si.stage = ToVkShaderStage(it->second.stage);
        si.module = it->second.module;
        si.pName = "main";
        stages.push_back(si);
    }
    if (stages.empty()) return PipelineHandle{};

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 256;  // glm::mat4 + padding, 与 kInstanceDescriptorDataSize 对齐
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        return PipelineHandle{};

    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attrs;
    for (const auto& b : desc.vertexBindings) {
        bindings.push_back({ b.binding, b.stride, b.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX });
    }
    for (const auto& a : desc.vertexAttributes) {
        attrs.push_back({ a.location, a.binding, ToVkFormat(a.format), a.offset });
    }

    VkPipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = ToVkPrimitiveTopology(desc.topology);
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster = {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = ToVkCullMode(desc.rasterization.cullEnable, desc.rasterization.frontFaceCCW);
    raster.frontFace = desc.rasterization.frontFaceCCW ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = desc.rasterization.lineWidth;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
        std::max(size_t(1), desc.colorAttachmentFormats.size()));
    for (size_t i = 0; i < blendAttachments.size(); ++i) {
        const BlendState* bs = (i < desc.blendStates.size()) ? &desc.blendStates[i] : nullptr;
        blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (bs && bs->blendEnable) {
            blendAttachments[i].blendEnable = VK_TRUE;
            blendAttachments[i].srcColorBlendFactor = ToVkBlendFactor(bs->srcColorBlendFactor);
            blendAttachments[i].dstColorBlendFactor = ToVkBlendFactor(bs->dstColorBlendFactor);
            blendAttachments[i].colorBlendOp = ToVkBlendOp(bs->colorBlendOp);
            blendAttachments[i].srcAlphaBlendFactor = ToVkBlendFactor(bs->srcAlphaBlendFactor);
            blendAttachments[i].dstAlphaBlendFactor = ToVkBlendFactor(bs->dstAlphaBlendFactor);
            blendAttachments[i].alphaBlendOp = ToVkBlendOp(bs->alphaBlendOp);
        } else {
            blendAttachments[i].blendEnable = VK_FALSE;
        }
    }

    VkPipelineColorBlendStateCreateInfo colorBlend = {};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.logicOpEnable = VK_FALSE;
    colorBlend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    colorBlend.pAttachments = blendAttachments.data();

    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = ToVkCompareOp(desc.depthStencil.depthCompareOp);
    depthStencil.stencilTestEnable = desc.depthStencil.stencilTestEnable ? VK_TRUE : VK_FALSE;

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    VkRenderPass rp = context_.GetRenderPass();
    if (!rp) {
        vkDestroyPipelineLayout(dev, pipelineLayout, nullptr);
        return PipelineHandle{};
    }
    pipelineInfo.renderPass = rp;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VkResult err = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    if (err != VK_SUCCESS) {
        vkDestroyPipelineLayout(dev, pipelineLayout, nullptr);
        return PipelineHandle{};
    }

    std::uint64_t id = nextPipelineId_++;
    pipelines_[id] = VulkanPipelineRes{ pipeline, pipelineLayout };
    PipelineHandle h;
    h.id = id;
    return h;
}

DescriptorSetHandle VulkanRenderDevice::CreateDescriptorSet(const DescriptorSetLayoutDesc& layout) {
    if (!context_.IsInitialized()) return DescriptorSetHandle{};
    if (layout.bindings.empty()) return DescriptorSetHandle{};

    VkDevice dev = context_.GetDevice();

    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    for (const auto& b : layout.bindings) {
        VkDescriptorSetLayoutBinding vb = {};
        vb.binding = b.binding;
        vb.descriptorType = ToVkDescriptorType(b.type);
        vb.descriptorCount = b.count;
        vb.stageFlags = ToVkShaderStage(b.visibility);
        vkBindings.push_back(vb);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
    layoutInfo.pBindings = vkBindings.data();

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &setLayout) != VK_SUCCESS)
        return DescriptorSetHandle{};

    uint32_t maxSets = 1;
    std::vector<VkDescriptorPoolSize> poolSizes;
    for (const auto& b : layout.bindings) {
        VkDescriptorType dt = ToVkDescriptorType(b.type);
        bool found = false;
        for (auto& ps : poolSizes) {
            if (ps.type == dt) { ps.descriptorCount += b.count; found = true; break; }
        }
        if (!found) poolSizes.push_back({ dt, b.count });
    }

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = maxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(dev, setLayout, nullptr);
        return DescriptorSetHandle{};
    }

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &setLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(dev, &allocInfo, &set) != VK_SUCCESS) {
        vkDestroyDescriptorPool(dev, pool, nullptr);
        vkDestroyDescriptorSetLayout(dev, setLayout, nullptr);
        return DescriptorSetHandle{};
    }

    std::uint64_t id = nextDescriptorSetId_++;
    descriptorSets_[id] = VulkanDescriptorSetRes{ set, setLayout, pool };
    DescriptorSetHandle h;
    h.id = id;
    return h;
}

// =============================================================================
// Destroy*
// =============================================================================

void VulkanRenderDevice::DestroyBuffer(BufferHandle handle) {
    if (!handle.IsValid()) return;
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end()) return;
    VulkanBufferRes& res = it->second;
#ifdef KALE_USE_VMA
    auto allocIt = bufferAllocations_.find(handle.id);
    if (allocIt != bufferAllocations_.end()) {
        VmaAllocator alloc = static_cast<VmaAllocator>(vmaAllocator_);
        if (alloc && res.mappedPtr) vmaUnmapMemory(alloc, static_cast<VmaAllocation>(allocIt->second));
        if (alloc) vmaDestroyBuffer(alloc, res.buffer, static_cast<VmaAllocation>(allocIt->second));
        bufferAllocations_.erase(allocIt);
        buffers_.erase(it);
        return;
    }
#endif
    if (res.mappedPtr) {
        vkUnmapMemory(context_.GetDevice(), res.memory);
        res.mappedPtr = nullptr;
    }
    DestroyVmaOrAllocBuffer(res.buffer, res.memory);
    buffers_.erase(it);
}

void VulkanRenderDevice::DestroyTexture(TextureHandle handle) {
    if (!handle.IsValid()) return;
    auto fbIt = depthFramebuffers_.find(handle.id);
    if (fbIt != depthFramebuffers_.end()) {
        if (fbIt->second != VK_NULL_HANDLE)
            vkDestroyFramebuffer(context_.GetDevice(), fbIt->second, nullptr);
        depthFramebuffers_.erase(fbIt);
    }
    auto it = textures_.find(handle.id);
    if (it == textures_.end()) return;
    VkDevice dev = context_.GetDevice();
    if (it->second.view != VK_NULL_HANDLE) vkDestroyImageView(dev, it->second.view, nullptr);
#ifdef KALE_USE_VMA
    auto allocIt = textureAllocations_.find(handle.id);
    if (allocIt != textureAllocations_.end()) {
        VmaAllocator alloc = static_cast<VmaAllocator>(vmaAllocator_);
        if (alloc) vmaDestroyImage(alloc, it->second.image, static_cast<VmaAllocation>(allocIt->second));
        textureAllocations_.erase(allocIt);
        textures_.erase(it);
        return;
    }
#endif
    if (it->second.image != VK_NULL_HANDLE) vkDestroyImage(dev, it->second.image, nullptr);
    if (it->second.memory != VK_NULL_HANDLE) vkFreeMemory(dev, it->second.memory, nullptr);
    textures_.erase(it);
}

void VulkanRenderDevice::DestroyShader(ShaderHandle handle) {
    if (!handle.IsValid()) return;
    auto it = shaders_.find(handle.id);
    if (it == shaders_.end()) return;
    if (it->second.module != VK_NULL_HANDLE)
        vkDestroyShaderModule(context_.GetDevice(), it->second.module, nullptr);
    shaders_.erase(it);
}

void VulkanRenderDevice::DestroyPipeline(PipelineHandle handle) {
    if (!handle.IsValid()) return;
    auto it = pipelines_.find(handle.id);
    if (it == pipelines_.end()) return;
    VkDevice dev = context_.GetDevice();
    if (it->second.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, it->second.pipeline, nullptr);
    if (it->second.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, it->second.layout, nullptr);
    pipelines_.erase(it);
}

void VulkanRenderDevice::DestroyDescriptorSet(DescriptorSetHandle handle) {
    if (!handle.IsValid()) return;
    if (instancePoolIds_.count(handle.id)) {
        ReleaseInstanceDescriptorSet(handle);
        return;
    }
    auto it = descriptorSets_.find(handle.id);
    if (it == descriptorSets_.end()) return;
    VkDevice dev = context_.GetDevice();
    if (it->second.set != VK_NULL_HANDLE) vkFreeDescriptorSets(dev, it->second.pool, 1, &it->second.set);
    if (it->second.pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(dev, it->second.pool, nullptr);
    if (it->second.layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, it->second.layout, nullptr);
    descriptorSets_.erase(it);
}

// =============================================================================
// Phase 9.2: 实例级 DescriptorSet 池
// =============================================================================

bool VulkanRenderDevice::CreateInstancePoolLayoutAndPool() {
    VkDevice dev = context_.GetDevice();
    constexpr uint32_t kInstancePoolMaxSets = 64;

    if (instanceDescriptorSetLayout_ == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding binding = {};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &binding;

        if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &instanceDescriptorSetLayout_) != VK_SUCCESS)
            return false;
    }

    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = kInstancePoolMaxSets;
    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = kInstancePoolMaxSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &pool) != VK_SUCCESS)
        return false;
    instanceDescriptorPools_.push_back(pool);
    return true;
}

void VulkanRenderDevice::DestroyInstancePoolResources() {
    VkDevice dev = context_.GetDevice();
    for (std::uint64_t bid : instancePoolBufferIds_) {
        auto it = buffers_.find(bid);
        if (it != buffers_.end()) {
            VulkanBufferRes& res = it->second;
            if (res.mappedPtr) {
                vkUnmapMemory(dev, res.memory);
                res.mappedPtr = nullptr;
            }
            DestroyVmaOrAllocBuffer(res.buffer, res.memory);
            buffers_.erase(it);
        }
    }
    instancePoolBufferIds_.clear();
    for (auto& entry : instancePoolFreeList_) {
        (void)entry;
    }
    instancePoolFreeList_.clear();
    for (VkDescriptorPool pool : instanceDescriptorPools_) {
        if (pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(dev, pool, nullptr);
    }
    instanceDescriptorPools_.clear();
    if (instanceDescriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, instanceDescriptorSetLayout_, nullptr);
        instanceDescriptorSetLayout_ = VK_NULL_HANDLE;
    }
    instancePoolIds_.clear();
}

DescriptorSetHandle VulkanRenderDevice::AcquireInstanceDescriptorSet(const void* instanceData,
                                                                      std::size_t size) {
    if (!context_.IsInitialized()) return DescriptorSetHandle{};
    if (size > kInstanceDescriptorDataSize) size = kInstanceDescriptorDataSize;

    if (!CreateInstancePoolLayoutAndPool()) return DescriptorSetHandle{};

    VkDevice dev = context_.GetDevice();
    std::uint64_t id = 0;
    VkDescriptorSet set = VK_NULL_HANDLE;
    BufferHandle bufferHandle{};
    VkDescriptorPool pool = VK_NULL_HANDLE;
    bool fromFreeList = false;

    if (!instancePoolFreeList_.empty()) {
        InstancePoolFreeEntry entry = instancePoolFreeList_.back();
        instancePoolFreeList_.pop_back();
        id = entry.id;
        set = entry.set;
        bufferHandle = entry.bufferHandle;
        pool = entry.pool;
        fromFreeList = true;
    } else {
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &instanceDescriptorSetLayout_;

        for (VkDescriptorPool p : instanceDescriptorPools_) {
            allocInfo.descriptorPool = p;
            if (vkAllocateDescriptorSets(dev, &allocInfo, &set) == VK_SUCCESS) {
                pool = p;
                break;
            }
        }
        if (set == VK_NULL_HANDLE) {
            if (!CreateInstancePoolLayoutAndPool()) return DescriptorSetHandle{};
            pool = instanceDescriptorPools_.back();
            allocInfo.descriptorPool = pool;
            if (vkAllocateDescriptorSets(dev, &allocInfo, &set) != VK_SUCCESS)
                return DescriptorSetHandle{};
        }
        id = nextInstanceDescriptorSetId_++;
        instancePoolIds_.insert(id);

        BufferDesc bufDesc;
        bufDesc.size = kInstanceDescriptorDataSize;
        bufDesc.usage = BufferUsage::Uniform;
        bufDesc.cpuVisible = true;
        bufferHandle = CreateBuffer(bufDesc, nullptr);
        if (!bufferHandle.IsValid()) {
            vkFreeDescriptorSets(dev, pool, 1, &set);
            return DescriptorSetHandle{};
        }
        instancePoolBufferIds_.insert(bufferHandle.id);
        descriptorSets_[id] = VulkanDescriptorSetRes{ set, instanceDescriptorSetLayout_, pool };
        WriteDescriptorSetBuffer(DescriptorSetHandle{id}, 0, bufferHandle, 0, kInstanceDescriptorDataSize);
    }

    instanceSetIdToBuffer_[id] = bufferHandle;
    if (fromFreeList)
        descriptorSets_[id] = VulkanDescriptorSetRes{ set, instanceDescriptorSetLayout_, pool };

    if (instanceData && size > 0)
        UpdateBuffer(bufferHandle, instanceData, size, 0);

    DescriptorSetHandle h;
    h.id = id;
    return h;
}

void VulkanRenderDevice::ReleaseInstanceDescriptorSet(DescriptorSetHandle handle) {
    if (!handle.IsValid()) return;
    auto it = descriptorSets_.find(handle.id);
    if (it == descriptorSets_.end()) return;
    if (instancePoolIds_.count(handle.id) == 0) return;

    BufferHandle bufferHandle{};
    auto bufIt = instanceSetIdToBuffer_.find(handle.id);
    if (bufIt != instanceSetIdToBuffer_.end()) {
        bufferHandle = bufIt->second;
        instanceSetIdToBuffer_.erase(bufIt);
    }

    InstancePoolFreeEntry entry;
    entry.id = handle.id;
    entry.set = it->second.set;
    entry.pool = it->second.pool;
    entry.bufferHandle = bufferHandle;
    descriptorSets_.erase(it);
    instancePoolFreeList_.push_back(entry);
}

void VulkanRenderDevice::WriteDescriptorSetTexture(DescriptorSetHandle set, std::uint32_t binding,
                                                    TextureHandle texture) {
    if (!set.IsValid() || !texture.IsValid()) return;
    auto dsIt = descriptorSets_.find(set.id);
    auto texIt = textures_.find(texture.id);
    if (dsIt == descriptorSets_.end() || texIt == textures_.end()) return;
    if (defaultSampler_ == VK_NULL_HANDLE) return;

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.sampler = defaultSampler_;
    imageInfo.imageView = texIt->second.view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = dsIt->second.set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(context_.GetDevice(), 1, &write, 0, nullptr);
}

void VulkanRenderDevice::WriteDescriptorSetBuffer(DescriptorSetHandle set, std::uint32_t binding,
                                                  BufferHandle buffer, std::size_t offset,
                                                  std::size_t range) {
    if (!set.IsValid() || !buffer.IsValid()) return;
    auto dsIt = descriptorSets_.find(set.id);
    auto bufIt = buffers_.find(buffer.id);
    if (dsIt == descriptorSets_.end() || bufIt == buffers_.end()) return;
    VkDeviceSize bufSize = static_cast<VkDeviceSize>(bufIt->second.size);
    VkDeviceSize vkOffset = static_cast<VkDeviceSize>(offset);
    VkDeviceSize vkRange = range > 0 ? static_cast<VkDeviceSize>(range) : (bufSize - vkOffset);
    if (vkOffset + vkRange > bufSize) return;

    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = bufIt->second.buffer;
    bufferInfo.offset = vkOffset;
    bufferInfo.range = vkRange;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = dsIt->second.set;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(context_.GetDevice(), 1, &write, 0, nullptr);
}

// =============================================================================
// UpdateBuffer / UpdateTexture
// =============================================================================

void VulkanRenderDevice::UpdateBuffer(BufferHandle handle, const void* data, std::size_t size,
                                     std::size_t offset) {
    if (!handle.IsValid() || !data || size == 0) return;
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end()) return;
    VulkanBufferRes& res = it->second;
    if (offset + size > res.size) return;

    VkDevice dev = context_.GetDevice();
    if (res.cpuVisible && res.mappedPtr) {
        memcpy(static_cast<char*>(res.mappedPtr) + offset, data, size);
        return;
    }

    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBufferCreateInfo stagingInfo = {};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = size;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &stagingInfo, nullptr, &stagingBuf) != VK_SUCCESS) return;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, stagingBuf, &mr);
    uint32_t mt = FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) { vkDestroyBuffer(dev, stagingBuf, nullptr); return; }
    VkMemoryAllocateInfo ma = {};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = mt;
    if (vkAllocateMemory(dev, &ma, nullptr, &stagingMem) != VK_SUCCESS) {
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        return;
    }
    vkBindBufferMemory(dev, stagingBuf, stagingMem, 0);
    void* mapped = nullptr;
    vkMapMemory(dev, stagingMem, 0, size, 0, &mapped);
    if (mapped) memcpy(mapped, data, size);
    vkUnmapMemory(dev, stagingMem);

    vkResetCommandBuffer(uploadCommandBuffer_, 0);
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(uploadCommandBuffer_, &bi);
    VkBufferCopy copy = {};
    copy.srcOffset = 0;
    copy.dstOffset = offset;
    copy.size = size;
    vkCmdCopyBuffer(uploadCommandBuffer_, stagingBuf, res.buffer, 1, &copy);
    vkEndCommandBuffer(uploadCommandBuffer_);

    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &uploadCommandBuffer_;
    vkQueueSubmit(context_.GetGraphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(context_.GetGraphicsQueue());

    vkDestroyBuffer(dev, stagingBuf, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);
}

void* VulkanRenderDevice::MapBuffer(BufferHandle handle, std::size_t offset, std::size_t size) {
    if (!handle.IsValid()) return nullptr;
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end() || !it->second.mappedPtr) return nullptr;
    VulkanBufferRes& res = it->second;
    if (offset + size > res.size) return nullptr;
    return static_cast<char*>(res.mappedPtr) + offset;
}

void VulkanRenderDevice::UnmapBuffer(BufferHandle handle) {
    (void)handle;
    /* 持久映射，在 DestroyBuffer 时统一 unmap */
}

void VulkanRenderDevice::UpdateTexture(TextureHandle handle, const void* data, std::uint32_t mipLevel) {
    if (!handle.IsValid() || !data) return;
    auto it = textures_.find(handle.id);
    if (it == textures_.end()) return;
    const VulkanTextureRes& res = it->second;
    const TextureDesc& desc = res.desc;
    if (mipLevel >= desc.mipLevels) return;

    VkDevice dev = context_.GetDevice();
    VkFormat format = ToVkFormat(desc.format);
    size_t pixelSize = 4;
    if (format == VK_FORMAT_R32G32B32A32_SFLOAT) pixelSize = 16;
    uint32_t w = std::max(1u, desc.width >> mipLevel);
    uint32_t h = std::max(1u, desc.height >> mipLevel);
    size_t layerSize = static_cast<size_t>(w) * h * pixelSize * desc.arrayLayers;

    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkBufferCreateInfo stagingInfo = {};
    stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingInfo.size = layerSize;
    stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &stagingInfo, nullptr, &stagingBuf) != VK_SUCCESS) return;
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(dev, stagingBuf, &mr);
    uint32_t mt = FindMemoryType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) { vkDestroyBuffer(dev, stagingBuf, nullptr); return; }
    VkMemoryAllocateInfo ma = {};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = mt;
    if (vkAllocateMemory(dev, &ma, nullptr, &stagingMem) != VK_SUCCESS) {
        vkDestroyBuffer(dev, stagingBuf, nullptr);
        return;
    }
    vkBindBufferMemory(dev, stagingBuf, stagingMem, 0);
    void* mapped = nullptr;
    vkMapMemory(dev, stagingMem, 0, layerSize, 0, &mapped);
    if (mapped) memcpy(mapped, data, layerSize);
    vkUnmapMemory(dev, stagingMem);

    vkResetCommandBuffer(uploadCommandBuffer_, 0);
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(uploadCommandBuffer_, &bi);

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = res.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = desc.arrayLayers;
    vkCmdPipelineBarrier(uploadCommandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = desc.arrayLayers;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { w, h, desc.depth };
    vkCmdCopyBufferToImage(uploadCommandBuffer_, stagingBuf, res.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(uploadCommandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(uploadCommandBuffer_);
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &uploadCommandBuffer_;
    vkQueueSubmit(context_.GetGraphicsQueue(), 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(context_.GetGraphicsQueue());

    vkDestroyBuffer(dev, stagingBuf, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);
}

// =============================================================================
// Upload 命令池
// =============================================================================

bool VulkanRenderDevice::CreateUploadCommandPoolAndBuffer() {
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = context_.GetGraphicsQueueFamilyIndex();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(context_.GetDevice(), &poolInfo, nullptr, &uploadCommandPool_) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = uploadCommandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(context_.GetDevice(), &allocInfo, &uploadCommandBuffer_) != VK_SUCCESS) {
        vkDestroyCommandPool(context_.GetDevice(), uploadCommandPool_, nullptr);
        uploadCommandPool_ = nullptr;
        return false;
    }
    return true;
}

bool VulkanRenderDevice::CreateFrameSyncObjects() {
    VkDevice dev = context_.GetDevice();
    frameFences_.resize(kMaxFramesInFlight);
    frameImageAvailableSemaphores_.resize(kMaxFramesInFlight);
    frameRenderFinishedSemaphores_.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0;
    for (std::uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
        if (vkCreateSemaphore(dev, &semInfo, nullptr, &frameImageAvailableSemaphores_[i]) != VK_SUCCESS) return false;
        if (vkCreateSemaphore(dev, &semInfo, nullptr, &frameRenderFinishedSemaphores_[i]) != VK_SUCCESS) return false;
        fenceInfo.flags = (i == 0) ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
        if (vkCreateFence(dev, &fenceInfo, nullptr, &frameFences_[i]) != VK_SUCCESS) return false;
    }
    return true;
}

void VulkanRenderDevice::DestroyFrameSyncObjects() {
    VkDevice dev = context_.GetDevice();
    for (auto s : frameImageAvailableSemaphores_) { if (s != VK_NULL_HANDLE) vkDestroySemaphore(dev, s, nullptr); }
    frameImageAvailableSemaphores_.clear();
    for (auto s : frameRenderFinishedSemaphores_) { if (s != VK_NULL_HANDLE) vkDestroySemaphore(dev, s, nullptr); }
    frameRenderFinishedSemaphores_.clear();
    for (auto f : frameFences_) { if (f != VK_NULL_HANDLE) vkDestroyFence(dev, f, nullptr); }
    frameFences_.clear();
    for (auto& [id, vkf] : fences_) { if (vkf != VK_NULL_HANDLE) vkDestroyFence(dev, vkf, nullptr); }
    fences_.clear();
    for (auto& [id, vks] : semaphores_) { if (vks != VK_NULL_HANDLE) vkDestroySemaphore(dev, vks, nullptr); }
    semaphores_.clear();
}

bool VulkanRenderDevice::CreateDefaultSampler() {
    VkDevice dev = context_.GetDevice();
    VkSamplerCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = VK_FILTER_LINEAR;
    info.minFilter = VK_FILTER_LINEAR;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    info.maxAnisotropy = 1.0f;
    info.minLod = 0.0f;
    info.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(dev, &info, nullptr, &defaultSampler_) != VK_SUCCESS)
        return false;
    return true;
}

void VulkanRenderDevice::DestroyDefaultSampler() {
    if (defaultSampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(context_.GetDevice(), defaultSampler_, nullptr);
        defaultSampler_ = VK_NULL_HANDLE;
    }
}

bool VulkanRenderDevice::CreateCommandPoolsAndBuffers() {
    // Vulkan 多线程录制约束 (phase9-9.6):
    // 1) 每线程使用独立 CommandPool，避免多线程同时分配/重置同一 Pool。
    // 2) 每 (threadIndex, frameIndex) 对应一个 VkCommandBuffer，保证每 buffer 仅单线程录制。
    // 3) RDI BeginCommandList(threadIndex) 仅允许 threadIndex < maxRecordingThreads，与上层对齐。
    VkDevice dev = context_.GetDevice();
    std::uint32_t queueFamily = context_.GetGraphicsQueueFamilyIndex();
    const std::uint32_t maxThreads = (maxRecordingThreads_ > 0u) ? maxRecordingThreads_ : 1u;
    commandPools_.resize(maxThreads);
    commandBuffers_.resize(maxThreads);
    commandListPool_.resize(maxThreads);

    for (std::uint32_t ti = 0; ti < maxThreads; ++ti) {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamily;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(dev, &poolInfo, nullptr, &commandPools_[ti]) != VK_SUCCESS) return false;

        commandBuffers_[ti].resize(kMaxFramesInFlight);
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPools_[ti];
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = kMaxFramesInFlight;
        if (vkAllocateCommandBuffers(dev, &allocInfo, commandBuffers_[ti].data()) != VK_SUCCESS) return false;

        commandListPool_[ti].resize(kMaxFramesInFlight);
        for (std::uint32_t fi = 0; fi < kMaxFramesInFlight; ++fi)
            commandListPool_[ti][fi] = std::make_unique<VulkanCommandList>(this, commandBuffers_[ti][fi], 0u);
    }
    return true;
}

void VulkanRenderDevice::DestroyCommandPoolsAndBuffers() {
    VkDevice dev = context_.GetDevice();
    commandListPool_.clear();
    for (auto& perThread : commandBuffers_) perThread.clear();
    commandBuffers_.clear();
    for (auto pool : commandPools_) {
        if (pool != VK_NULL_HANDLE) vkDestroyCommandPool(dev, pool, nullptr);
    }
    commandPools_.clear();
}

void VulkanRenderDevice::DestroyUploadCommandPoolAndBuffer() {
    VkDevice dev = context_.GetDevice();
    if (uploadCommandPool_ != VK_NULL_HANDLE) {
        if (uploadCommandBuffer_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(dev, uploadCommandPool_, 1, &uploadCommandBuffer_);
            uploadCommandBuffer_ = VK_NULL_HANDLE;
        }
        vkDestroyCommandPool(dev, uploadCommandPool_, nullptr);
        uploadCommandPool_ = VK_NULL_HANDLE;
    }
}

// =============================================================================
// 命令与同步（Phase 2.5）
// =============================================================================

CommandList* VulkanRenderDevice::BeginCommandList(std::uint32_t threadIndex) {
    // 约束：同一 VkCommandBuffer 仅由单线程录制；threadIndex 对应独立 CommandPool 与 buffer。
    if (!context_.IsInitialized()) return nullptr;
    if (threadIndex >= commandListPool_.size()) return nullptr;
    std::uint32_t frameIndex = currentFrameIndex_ % kMaxFramesInFlight;
    VulkanCommandList* cmd = commandListPool_[threadIndex][frameIndex].get();
    VkCommandBuffer buf = commandBuffers_[threadIndex][frameIndex];
    vkResetCommandBuffer(buf, 0);
    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(buf, &beginInfo) != VK_SUCCESS) return nullptr;
    cmd->SetSwapchainImageIndex(currentImageIndex_);
    return cmd;
}

void VulkanRenderDevice::EndCommandList(CommandList* cmd) {
    if (!cmd) return;
    auto* vc = static_cast<VulkanCommandList*>(cmd);
    vkEndCommandBuffer(vc->GetCommandBuffer());
}

void VulkanRenderDevice::Submit(const std::vector<CommandList*>& cmdLists,
                               const std::vector<SemaphoreHandle>& waitSemaphores,
                               const std::vector<SemaphoreHandle>& signalSemaphores,
                               FenceHandle fence) {
    if (!context_.IsInitialized() || cmdLists.empty()) return;
    VkDevice dev = context_.GetDevice();
    VkQueue queue = context_.GetGraphicsQueue();
    std::vector<VkCommandBuffer> vkBuffers;
    vkBuffers.reserve(cmdLists.size());
    for (CommandList* c : cmdLists) {
        auto* vc = static_cast<VulkanCommandList*>(c);
        vkBuffers.push_back(vc->GetCommandBuffer());
    }

    std::vector<VkSemaphore> waitSems;
    std::vector<VkPipelineStageFlags> waitStages;
    if (waitSemaphores.empty()) {
        waitSems.push_back(frameImageAvailableSemaphores_[currentFrameIndex_ % kMaxFramesInFlight]);
        waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    } else {
        for (const auto& h : waitSemaphores) {
            auto it = semaphores_.find(h.id);
            if (it != semaphores_.end()) { waitSems.push_back(it->second); waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT); }
        }
    }
    std::vector<VkSemaphore> signalSems;
    if (signalSemaphores.empty())
        signalSems.push_back(frameRenderFinishedSemaphores_[currentFrameIndex_ % kMaxFramesInFlight]);
    else {
        for (const auto& h : signalSemaphores) {
            auto it = semaphores_.find(h.id);
            if (it != semaphores_.end()) signalSems.push_back(it->second);
        }
    }
    VkFence submitFence = VK_NULL_HANDLE;
    if (fence.IsValid()) {
        auto it = fences_.find(fence.id);
        if (it != fences_.end()) submitFence = it->second;
    } else
        submitFence = frameFences_[currentFrameIndex_ % kMaxFramesInFlight];

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = static_cast<std::uint32_t>(waitSems.size());
    submitInfo.pWaitSemaphores = waitSems.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = static_cast<std::uint32_t>(vkBuffers.size());
    submitInfo.pCommandBuffers = vkBuffers.data();
    submitInfo.signalSemaphoreCount = static_cast<std::uint32_t>(signalSems.size());
    submitInfo.pSignalSemaphores = signalSems.data();
    vkQueueSubmit(queue, 1, &submitInfo, submitFence);
}

void VulkanRenderDevice::WaitIdle() {
    if (context_.IsInitialized())
        vkDeviceWaitIdle(context_.GetDevice());
}

FenceHandle VulkanRenderDevice::CreateFence(bool signaled) {
    if (!context_.IsInitialized()) return FenceHandle{};
    VkFenceCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0u;
    VkFence f = VK_NULL_HANDLE;
    if (vkCreateFence(context_.GetDevice(), &info, nullptr, &f) != VK_SUCCESS) return FenceHandle{};
    std::uint64_t id = nextFenceId_++;
    fences_[id] = f;
    FenceHandle h;
    h.id = id;
    return h;
}

void VulkanRenderDevice::WaitForFence(FenceHandle fence, std::uint64_t timeout) {
    if (!fence.IsValid() || !context_.IsInitialized()) return;
    auto it = fences_.find(fence.id);
    if (it != fences_.end())
        vkWaitForFences(context_.GetDevice(), 1, &it->second, VK_TRUE, timeout);
}

void VulkanRenderDevice::ResetFence(FenceHandle fence) {
    if (!fence.IsValid() || !context_.IsInitialized()) return;
    auto it = fences_.find(fence.id);
    if (it != fences_.end())
        vkResetFences(context_.GetDevice(), 1, &it->second);
}

bool VulkanRenderDevice::IsFenceSignaled(FenceHandle fence) const {
    if (!fence.IsValid() || !context_.IsInitialized()) return false;
    auto it = fences_.find(fence.id);
    if (it == fences_.end()) return false;
    VkResult r = vkGetFenceStatus(context_.GetDevice(), it->second);
    return (r == VK_SUCCESS);
}

SemaphoreHandle VulkanRenderDevice::CreateSemaphore() {
    if (!context_.IsInitialized()) return SemaphoreHandle{};
    VkSemaphoreCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore s = VK_NULL_HANDLE;
    if (vkCreateSemaphore(context_.GetDevice(), &info, nullptr, &s) != VK_SUCCESS) return SemaphoreHandle{};
    std::uint64_t id = nextSemaphoreId_++;
    semaphores_[id] = s;
    SemaphoreHandle h;
    h.id = id;
    return h;
}

std::uint32_t VulkanRenderDevice::AcquireNextImage() {
    if (!context_.IsInitialized()) return IRenderDevice::kInvalidSwapchainImageIndex;
    VkDevice dev = context_.GetDevice();
    std::uint32_t frameIndex = currentFrameIndex_ % kMaxFramesInFlight;
    vkWaitForFences(dev, 1, &frameFences_[frameIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(dev, 1, &frameFences_[frameIndex]);
    std::uint32_t imageIndex = 0;
    VkResult err = vkAcquireNextImageKHR(dev, context_.GetSwapchain(), UINT64_MAX,
                                         frameImageAvailableSemaphores_[frameIndex], VK_NULL_HANDLE, &imageIndex);
    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
        if (!context_.RecreateSwapchain(width_, height_)) return IRenderDevice::kInvalidSwapchainImageIndex;
        err = vkAcquireNextImageKHR(dev, context_.GetSwapchain(), UINT64_MAX,
                                    frameImageAvailableSemaphores_[frameIndex], VK_NULL_HANDLE, &imageIndex);
    }
    if (err != VK_SUCCESS && err != VK_SUBOPTIMAL_KHR) return IRenderDevice::kInvalidSwapchainImageIndex;
    currentImageIndex_ = imageIndex;
    return imageIndex;
}

void VulkanRenderDevice::Present() {
    if (!context_.IsInitialized()) return;
    VkSwapchainKHR swapchain = context_.GetSwapchain();
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    std::uint32_t frameIndex = currentFrameIndex_ % kMaxFramesInFlight;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frameRenderFinishedSemaphores_[frameIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &currentImageIndex_;
    VkResult err = vkQueuePresentKHR(context_.GetPresentQueue(), &presentInfo);
    if (err == VK_ERROR_OUT_OF_DATE_KHR) {
        context_.RecreateSwapchain(width_, height_);
        return;
    }
    if (err == VK_SUCCESS || err == VK_SUBOPTIMAL_KHR)
        currentFrameIndex_ = (currentFrameIndex_ + 1) % kMaxFramesInFlight;
}

TextureHandle VulkanRenderDevice::GetBackBuffer() {
    TextureHandle h;
    h.id = currentImageIndex_ + 1;
    return h;
}

std::uint32_t VulkanRenderDevice::GetCurrentFrameIndex() const {
    return currentFrameIndex_ % kMaxFramesInFlight;
}

void VulkanRenderDevice::SetExtent(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
}

const DeviceCapabilities& VulkanRenderDevice::GetCapabilities() const {
    return capabilities_;
}

// =============================================================================
// VulkanCommandList 实现
// =============================================================================

VulkanCommandList::VulkanCommandList(VulkanRenderDevice* device, VkCommandBuffer buffer,
                                     std::uint32_t swapchainImageIndex)
    : device_(device), commandBuffer_(buffer), swapchainImageIndex_(swapchainImageIndex) {}

void VulkanCommandList::BeginRenderPass(const std::vector<TextureHandle>& colorAttachments,
                                        TextureHandle depthAttachment) {
    if (!device_ || !commandBuffer_) return;

    if (!colorAttachments.empty()) {
        VulkanContext* ctx = device_->GetContext();
        VkRenderPass rp = ctx->GetRenderPass();
        if (!rp) return;
        std::uint32_t scCount = ctx->GetSwapchainImageCount();
        std::uint32_t width = ctx->GetSwapchainWidth();
        std::uint32_t height = ctx->GetSwapchainHeight();
        VkFramebuffer fb = VK_NULL_HANDLE;
        if (colorAttachments[0].id >= 1 && colorAttachments[0].id <= scCount)
            fb = ctx->GetFramebuffer(static_cast<std::uint32_t>(colorAttachments[0].id - 1));
        if (!fb) return;
        VkClearValue clear = {};
        clear.color = {{ 0.0f, 0.0f, 0.1f, 1.0f }};
        VkRenderPassBeginInfo rpBegin = {};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = rp;
        rpBegin.framebuffer = fb;
        rpBegin.renderArea = {{ 0, 0 }, { width, height }};
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clear;
        vkCmdBeginRenderPass(commandBuffer_, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        return;
    }

    if (depthAttachment.IsValid()) {
        auto texIt = device_->textures_.find(depthAttachment.id);
        if (texIt == device_->textures_.end()) return;
        VkFormat depthFormat = ToVkFormat(texIt->second.desc.format);
        VkRenderPass rp = device_->GetOrCreateDepthOnlyRenderPass(depthFormat);
        VkFramebuffer fb = device_->GetOrCreateDepthFramebuffer(depthAttachment);
        if (rp == VK_NULL_HANDLE || fb == VK_NULL_HANDLE) return;
        std::uint32_t width = texIt->second.desc.width;
        std::uint32_t height = texIt->second.desc.height;
        VkClearValue clear = {};
        clear.depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo rpBegin = {};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = rp;
        rpBegin.framebuffer = fb;
        rpBegin.renderArea = {{ 0, 0 }, { width, height }};
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clear;
        vkCmdBeginRenderPass(commandBuffer_, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    }
}

void VulkanCommandList::EndRenderPass() {
    if (commandBuffer_) vkCmdEndRenderPass(commandBuffer_);
}

void VulkanCommandList::BindPipeline(PipelineHandle pipeline) {
    if (!device_ || !commandBuffer_ || !pipeline.IsValid()) return;
    auto it = device_->pipelines_.find(pipeline.id);
    if (it == device_->pipelines_.end()) return;
    currentPipelineLayout_ = it->second.layout;
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, it->second.pipeline);
}

void VulkanCommandList::BindDescriptorSet(std::uint32_t set, DescriptorSetHandle descriptorSet) {
    if (!device_ || !commandBuffer_ || !descriptorSet.IsValid() || !currentPipelineLayout_) return;
    auto it = device_->descriptorSets_.find(descriptorSet.id);
    if (it == device_->descriptorSets_.end()) return;
    vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            currentPipelineLayout_, set, 1, &it->second.set, 0, nullptr);
}

void VulkanCommandList::BindVertexBuffer(std::uint32_t binding, BufferHandle buffer, std::size_t offset) {
    if (!device_ || !commandBuffer_ || !buffer.IsValid()) return;
    auto it = device_->buffers_.find(buffer.id);
    if (it == device_->buffers_.end()) return;
    VkDeviceSize o = offset;
    vkCmdBindVertexBuffers(commandBuffer_, binding, 1, &it->second.buffer, &o);
}

void VulkanCommandList::BindIndexBuffer(BufferHandle buffer, std::size_t offset, bool is16Bit) {
    if (!device_ || !commandBuffer_ || !buffer.IsValid()) return;
    auto it = device_->buffers_.find(buffer.id);
    if (it == device_->buffers_.end()) return;
    vkCmdBindIndexBuffer(commandBuffer_, it->second.buffer, offset,
                         is16Bit ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void VulkanCommandList::SetPushConstants(const void* data, std::size_t size, std::size_t offset) {
    if (!device_ || !commandBuffer_ || !data || !currentPipelineLayout_) return;
    vkCmdPushConstants(commandBuffer_, currentPipelineLayout_, VK_SHADER_STAGE_ALL, static_cast<std::uint32_t>(offset),
                       static_cast<std::uint32_t>(size), data);
}

void VulkanCommandList::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount,
                             std::uint32_t firstVertex, std::uint32_t firstInstance) {
    if (commandBuffer_)
        vkCmdDraw(commandBuffer_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
                                   std::uint32_t firstIndex, std::int32_t vertexOffset,
                                   std::uint32_t firstInstance) {
    if (commandBuffer_)
        vkCmdDrawIndexed(commandBuffer_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                                 std::uint32_t groupCountZ) {
    if (commandBuffer_)
        vkCmdDispatch(commandBuffer_, groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandList::CopyBufferToBuffer(BufferHandle srcBuffer, std::size_t srcOffset,
                                           BufferHandle dstBuffer, std::size_t dstOffset,
                                           std::size_t size) {
    if (!device_ || !commandBuffer_ || size == 0) return;
    auto sit = device_->buffers_.find(srcBuffer.id);
    auto dit = device_->buffers_.find(dstBuffer.id);
    if (sit == device_->buffers_.end() || dit == device_->buffers_.end()) return;
    VkBufferCopy copy = {};
    copy.srcOffset = srcOffset;
    copy.dstOffset = dstOffset;
    copy.size = size;
    vkCmdCopyBuffer(commandBuffer_, sit->second.buffer, dit->second.buffer, 1, &copy);
}

void VulkanCommandList::CopyBufferToTexture(BufferHandle srcBuffer, std::size_t srcOffset,
                                            TextureHandle dstTexture, std::uint32_t mipLevel,
                                            std::uint32_t width, std::uint32_t height,
                                            std::uint32_t depth) {
    if (!device_ || !commandBuffer_ || width == 0 || height == 0 || depth == 0) return;
    auto bit = device_->buffers_.find(srcBuffer.id);
    auto tit = device_->textures_.find(dstTexture.id);
    if (bit == device_->buffers_.end() || tit == device_->textures_.end()) return;
    const VulkanTextureRes& res = tit->second;
    VkImage image = res.image;
    const TextureDesc& desc = res.desc;
    if (mipLevel >= desc.mipLevels) return;

    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (desc.format == Format::D16 || desc.format == Format::D24 || desc.format == Format::D32)
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    else if (desc.format == Format::D24S8 || desc.format == Format::D32S8)
        aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = desc.arrayLayers;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region = {};
    region.bufferOffset = srcOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = aspect;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = desc.arrayLayers;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, depth};
    vkCmdCopyBufferToImage(commandBuffer_, bit->second.buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanCommandList::CopyTextureToTexture(TextureHandle srcTexture, TextureHandle dstTexture,
                                             std::uint32_t width, std::uint32_t height) {
    if (!device_ || !commandBuffer_ || width == 0 || height == 0) return;
    VulkanContext* ctx = device_->GetContext();
    if (!ctx) return;

    auto sit = device_->textures_.find(srcTexture.id);
    if (sit == device_->textures_.end()) return;

    VkImage srcImage = sit->second.image;
    VkImage dstImage = VK_NULL_HANDLE;
    bool dstIsSwapchain = false;
    TextureHandle backBuffer = device_->GetBackBuffer();
    if (dstTexture.id == backBuffer.id) {
        dstImage = ctx->GetSwapchainImage(swapchainImageIndex_);
        dstIsSwapchain = true;
    } else {
        auto dit = device_->textures_.find(dstTexture.id);
        if (dit == device_->textures_.end()) return;
        dstImage = dit->second.image;
    }
    if (!srcImage || !dstImage) return;

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.image = srcImage;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    barrier.image = dstImage;
    barrier.oldLayout = dstIsSwapchain ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkImageCopy region = {};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = {0, 0, 0};
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = {0, 0, 0};
    region.extent = {width, height, 1};
    vkCmdCopyImage(commandBuffer_, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.image = srcImage;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

    barrier.image = dstImage;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = dstIsSwapchain ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = dstIsSwapchain ? 0 : VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         dstIsSwapchain ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanCommandList::Barrier(const std::vector<TextureHandle>& textures) {
    if (!device_ || !commandBuffer_ || textures.empty()) return;
    std::vector<VkImageMemoryBarrier> barriers;
    for (const auto& th : textures) {
        auto it = device_->textures_.find(th.id);
        if (it == device_->textures_.end()) continue;
        VkImageMemoryBarrier b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.image = it->second.image;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = b.subresourceRange.layerCount = 1;
        barriers.push_back(b);
    }
    if (!barriers.empty())
        vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             0, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(barriers.size()), barriers.data());
}

void VulkanCommandList::ClearColor(TextureHandle texture, const float color[4]) {
    (void)texture;
    (void)color;
}

void VulkanCommandList::ClearDepth(TextureHandle texture, float depth, std::uint8_t stencil) {
    (void)texture;
    (void)depth;
    (void)stencil;
}

void VulkanCommandList::SetViewport(float x, float y, float width, float height,
                                    float minDepth, float maxDepth) {
    if (!commandBuffer_) return;
    VkViewport vp = { x, y, width, height, minDepth, maxDepth };
    vkCmdSetViewport(commandBuffer_, 0, 1, &vp);
}

void VulkanCommandList::SetScissor(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height) {
    if (!commandBuffer_) return;
    VkRect2D r = { { x, y }, { width, height } };
    vkCmdSetScissor(commandBuffer_, 0, 1, &r);
}

}  // namespace kale_device
