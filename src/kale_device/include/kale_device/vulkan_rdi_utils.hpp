/**
 * @file vulkan_rdi_utils.hpp
 * @brief RDI 类型到 Vulkan 的转换与资源存储结构（Phase 2.4）
 */

#pragma once

#include <kale_device/rdi_types.hpp>
#include <vulkan/vulkan.h>

#include <unordered_map>

namespace kale_device {

// --- Format / Usage 转换 ---
VkFormat ToVkFormat(Format f);
/** VkFormat -> RDI Format（KTX/外部格式加载时使用；未映射返回 Format::Undefined） */
Format FromVkFormat(VkFormat vkFormat);
VkBufferUsageFlags ToVkBufferUsage(BufferUsage u);
VkImageUsageFlags ToVkImageUsage(TextureUsage u);
VkShaderStageFlagBits ToVkShaderStage(ShaderStage s);
VkPrimitiveTopology ToVkPrimitiveTopology(PrimitiveTopology t);
VkCompareOp ToVkCompareOp(CompareOp o);
VkBlendFactor ToVkBlendFactor(BlendFactor f);
VkBlendOp ToVkBlendOp(BlendOp o);
VkDescriptorType ToVkDescriptorType(DescriptorType t);
VkCullModeFlags ToVkCullMode(bool cullEnable, bool frontFaceCCW);

// --- 资源存储（Vulkan 句柄与元数据）---
struct VulkanBufferRes {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    bool cpuVisible = false;
    void* mappedPtr = nullptr;  // 持久映射（仅 cpuVisible 时有效）
};

struct VulkanTextureRes {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    TextureDesc desc;
};

struct VulkanShaderRes {
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage stage = ShaderStage::Vertex;
};

struct VulkanPipelineRes {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
};

struct VulkanDescriptorSetRes {
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;  // 每个 layout 可对应一个 pool 或共享
};

}  // namespace kale_device
