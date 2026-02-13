/**
 * @file vulkan_rdi_utils.cpp
 * @brief RDI -> Vulkan 转换实现
 */

#include <kale_device/vulkan_rdi_utils.hpp>

namespace kale_device {

VkFormat ToVkFormat(Format f) {
    switch (f) {
        case Format::R8_UNORM: return VK_FORMAT_R8_UNORM;
        case Format::RG8_UNORM: return VK_FORMAT_R8G8_UNORM;
        case Format::RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::R16F: return VK_FORMAT_R16_SFLOAT;
        case Format::RG16F: return VK_FORMAT_R16G16_SFLOAT;
        case Format::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::R32F: return VK_FORMAT_R32_SFLOAT;
        case Format::RG32F: return VK_FORMAT_R32G32_SFLOAT;
        case Format::RGB32F: return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::D16: return VK_FORMAT_D16_UNORM;
        case Format::D24: return VK_FORMAT_X8_D24_UNORM_PACK32;
        case Format::D32: return VK_FORMAT_D32_SFLOAT;
        case Format::D24S8: return VK_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32S8: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case Format::BC1: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        case Format::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
        case Format::BC5: return VK_FORMAT_BC5_UNORM_BLOCK;
        case Format::BC7: return VK_FORMAT_BC7_UNORM_BLOCK;
        default: return VK_FORMAT_UNDEFINED;
    }
}

Format FromVkFormat(VkFormat vkFormat) {
    switch (vkFormat) {
        case VK_FORMAT_R8_UNORM: return Format::R8_UNORM;
        case VK_FORMAT_R8G8_UNORM: return Format::RG8_UNORM;
        case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB: return Format::RGBA8_SRGB;
        case VK_FORMAT_R16_SFLOAT: return Format::R16F;
        case VK_FORMAT_R16G16_SFLOAT: return Format::RG16F;
        case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::RGBA16F;
        case VK_FORMAT_R32_SFLOAT: return Format::R32F;
        case VK_FORMAT_R32G32_SFLOAT: return Format::RG32F;
        case VK_FORMAT_R32G32B32_SFLOAT: return Format::RGB32F;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return Format::RGBA32F;
        case VK_FORMAT_D16_UNORM: return Format::D16;
        case VK_FORMAT_X8_D24_UNORM_PACK32: return Format::D24;
        case VK_FORMAT_D32_SFLOAT: return Format::D32;
        case VK_FORMAT_D24_UNORM_S8_UINT: return Format::D24S8;
        case VK_FORMAT_D32_SFLOAT_S8_UINT: return Format::D32S8;
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return Format::BC1;
        case VK_FORMAT_BC3_UNORM_BLOCK: return Format::BC3;
        case VK_FORMAT_BC5_UNORM_BLOCK: return Format::BC5;
        case VK_FORMAT_BC7_UNORM_BLOCK: return Format::BC7;
        default: return Format::Undefined;
    }
}

VkBufferUsageFlags ToVkBufferUsage(BufferUsage u) {
    VkBufferUsageFlags f = 0;
    if (HasBufferUsage(u, BufferUsage::Vertex)) f |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (HasBufferUsage(u, BufferUsage::Index)) f |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (HasBufferUsage(u, BufferUsage::Uniform)) f |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (HasBufferUsage(u, BufferUsage::Storage)) f |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (HasBufferUsage(u, BufferUsage::Transfer)) f |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return f;
}

VkImageUsageFlags ToVkImageUsage(TextureUsage u) {
    VkImageUsageFlags f = 0;
    if (HasTextureUsage(u, TextureUsage::Sampled)) f |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (HasTextureUsage(u, TextureUsage::Storage)) f |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (HasTextureUsage(u, TextureUsage::ColorAttachment)) f |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (HasTextureUsage(u, TextureUsage::DepthAttachment)) f |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (HasTextureUsage(u, TextureUsage::Transfer)) f |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return f;
}

VkShaderStageFlagBits ToVkShaderStage(ShaderStage s) {
    switch (s) {
        case ShaderStage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
        case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
        case ShaderStage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
        case ShaderStage::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
        case ShaderStage::TessControl: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        case ShaderStage::TessEvaluation: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        default: return VK_SHADER_STAGE_VERTEX_BIT;
    }
}

VkPrimitiveTopology ToVkPrimitiveTopology(PrimitiveTopology t) {
    switch (t) {
        case PrimitiveTopology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkCompareOp ToVkCompareOp(CompareOp o) {
    switch (o) {
        case CompareOp::Never: return VK_COMPARE_OP_NEVER;
        case CompareOp::Less: return VK_COMPARE_OP_LESS;
        case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
        case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
        case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
        default: return VK_COMPARE_OP_LESS;
    }
}

VkBlendFactor ToVkBlendFactor(BlendFactor f) {
    switch (f) {
        case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
        case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstAlpha: return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::OneMinusDstAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendOp ToVkBlendOp(BlendOp o) {
    switch (o) {
        case BlendOp::Add: return VK_BLEND_OP_ADD;
        case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::Min: return VK_BLEND_OP_MIN;
        case BlendOp::Max: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}

VkDescriptorType ToVkDescriptorType(DescriptorType t) {
    switch (t) {
        case DescriptorType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DescriptorType::SampledImage: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        case DescriptorType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
        case DescriptorType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        case DescriptorType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        case DescriptorType::StorageImage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        default: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    }
}

VkCullModeFlags ToVkCullMode(bool cullEnable, bool frontFaceCCW) {
    if (!cullEnable) return VK_CULL_MODE_NONE;
    return frontFaceCCW ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
}

}  // namespace kale_device
