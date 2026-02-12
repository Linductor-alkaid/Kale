/**
 * @file vulkan_render_device.cpp
 * @brief VulkanRenderDevice 实现（Phase 2.4 资源创建/销毁/更新）
 */

#include <kale_device/vulkan_render_device.hpp>
#include <vulkan/vulkan.h>

#include <cstring>
#include <algorithm>

namespace kale_device {

// =============================================================================
// 生命周期
// =============================================================================

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

    if (!CreateUploadCommandPoolAndBuffer()) {
        Shutdown();
        return false;
    }

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
    if (!context_.IsInitialized()) return;

    VkDevice dev = context_.GetDevice();

    for (auto& [id, res] : buffers_) {
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

    for (auto& [id, res] : shaders_) {
        if (res.module != VK_NULL_HANDLE) vkDestroyShaderModule(dev, res.module, nullptr);
    }
    shaders_.clear();

    for (auto& [id, res] : pipelines_) {
        if (res.pipeline != VK_NULL_HANDLE) vkDestroyPipeline(dev, res.pipeline, nullptr);
        if (res.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(dev, res.layout, nullptr);
    }
    pipelines_.clear();

    for (auto& [id, res] : descriptorSets_) {
        if (res.set != VK_NULL_HANDLE) vkFreeDescriptorSets(dev, res.pool, 1, &res.set);
        if (res.pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(dev, res.pool, nullptr);
        if (res.layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, res.layout, nullptr);
    }
    descriptorSets_.clear();

    DestroyUploadCommandPoolAndBuffer();
    context_.Shutdown();
    capabilities_ = DeviceCapabilities{};
    currentImageIndex_ = 0;
    currentFrameIndex_ = 0;
}

const std::string& VulkanRenderDevice::GetLastError() const {
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

bool VulkanRenderDevice::CreateVmaOrAllocBuffer(const BufferDesc& desc, const void* data,
                                                VkBuffer* outBuffer, VkDeviceMemory* outMemory,
                                                VkDeviceSize* outSize) {
    VkDevice dev = context_.GetDevice();
    VkDeviceSize size = desc.size;
    if (size == 0) return false;

    VkBufferUsageFlags usage = ToVkBufferUsage(desc.usage);
    if (data && !desc.cpuVisible)
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkBufferCreateInfo bufInfo = {};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = size;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

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
        void* mapped = nullptr;
        vkMapMemory(dev, *outMemory, 0, size, 0, &mapped);
        if (mapped) {
            memcpy(mapped, data, size);
            vkUnmapMemory(dev, *outMemory);
        }
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

    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    if (!CreateVmaOrAllocBuffer(desc, data, &buf, &mem, &size)) {
        return BufferHandle{};
    }

    std::uint64_t id = nextBufferId_++;
    buffers_[id] = VulkanBufferRes{ buf, mem, size, desc.cpuVisible };
    BufferHandle h;
    h.id = id;
    return h;
}

bool VulkanRenderDevice::CreateTextureInternal(const TextureDesc& desc, const void* data,
                                               VkImage* outImage, VkDeviceMemory* outMemory,
                                               VkImageView* outView) {
    VkDevice dev = context_.GetDevice();
    VkFormat format = ToVkFormat(desc.format);
    if (format == VK_FORMAT_UNDEFINED) return false;

    VkImageUsageFlags usage = ToVkImageUsage(desc.usage);
    if (data) usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

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

    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    if (!CreateTextureInternal(desc, data, &image, &memory, &view)) {
        return TextureHandle{};
    }

    std::uint64_t id = nextTextureId_++;
    VulkanTextureRes res;
    res.image = image;
    res.memory = memory;
    res.view = view;
    res.desc = desc;
    textures_[id] = std::move(res);
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
    VkPipelineLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pushConstantRangeCount = 0;
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
    DestroyVmaOrAllocBuffer(it->second.buffer, it->second.memory);
    buffers_.erase(it);
}

void VulkanRenderDevice::DestroyTexture(TextureHandle handle) {
    if (!handle.IsValid()) return;
    auto it = textures_.find(handle.id);
    if (it == textures_.end()) return;
    VkDevice dev = context_.GetDevice();
    if (it->second.view != VK_NULL_HANDLE) vkDestroyImageView(dev, it->second.view, nullptr);
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
    auto it = descriptorSets_.find(handle.id);
    if (it == descriptorSets_.end()) return;
    VkDevice dev = context_.GetDevice();
    if (it->second.set != VK_NULL_HANDLE) vkFreeDescriptorSets(dev, it->second.pool, 1, &it->second.set);
    if (it->second.pool != VK_NULL_HANDLE) vkDestroyDescriptorPool(dev, it->second.pool, nullptr);
    if (it->second.layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(dev, it->second.layout, nullptr);
    descriptorSets_.erase(it);
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
    if (res.cpuVisible) {
        void* mapped = nullptr;
        vkMapMemory(dev, res.memory, offset, size, 0, &mapped);
        if (mapped) {
            memcpy(mapped, data, size);
            vkUnmapMemory(dev, res.memory);
        }
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
// 命令 / 同步 / 交换链（占位，Phase 2.5/2.6）
// =============================================================================

CommandList* VulkanRenderDevice::BeginCommandList(std::uint32_t) {
    return nullptr;
}

void VulkanRenderDevice::EndCommandList(CommandList*) {}

void VulkanRenderDevice::Submit(const std::vector<CommandList*>&,
                               const std::vector<SemaphoreHandle>&,
                               const std::vector<SemaphoreHandle>&,
                               FenceHandle) {}

void VulkanRenderDevice::WaitIdle() {
    if (context_.IsInitialized())
        vkDeviceWaitIdle(context_.GetDevice());
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
    h.id = currentImageIndex_ + 1;
    return h;
}

std::uint32_t VulkanRenderDevice::GetCurrentFrameIndex() const {
    return currentFrameIndex_;
}

const DeviceCapabilities& VulkanRenderDevice::GetCapabilities() const {
    return capabilities_;
}

}  // namespace kale_device
