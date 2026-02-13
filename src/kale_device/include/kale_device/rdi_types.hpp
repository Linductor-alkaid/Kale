/**
 * @file rdi_types.hpp
 * @brief RDI (Rendering Device Interface) 资源句柄与描述符类型定义
 *
 * 设备抽象层类型：Handle<T>、资源描述符、管线状态等。
 * 与 device_abstraction_layer_design.md 对齐。
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kale_device {

// =============================================================================
// 资源句柄
// =============================================================================

/** 类型安全资源句柄，id=0 表示无效 */
template <typename Tag>
struct Handle {
    std::uint64_t id = 0;

    bool IsValid() const { return id != 0; }
    bool operator==(const Handle& other) const { return id == other.id; }
    bool operator!=(const Handle& other) const { return id != other.id; }
};

struct Buffer_Tag {};
struct Texture_Tag {};
struct Shader_Tag {};
struct Pipeline_Tag {};
struct DescriptorSet_Tag {};
struct Fence_Tag {};
struct Semaphore_Tag {};

using BufferHandle       = Handle<Buffer_Tag>;
using TextureHandle      = Handle<Texture_Tag>;
using ShaderHandle       = Handle<Shader_Tag>;
using PipelineHandle     = Handle<Pipeline_Tag>;
using DescriptorSetHandle = Handle<DescriptorSet_Tag>;
using FenceHandle        = Handle<Fence_Tag>;
using SemaphoreHandle    = Handle<Semaphore_Tag>;

// =============================================================================
// 格式与用途枚举
// =============================================================================

enum class Format {
    Undefined,
    R8_UNORM,
    RG8_UNORM,
    RGBA8_UNORM,
    RGBA8_SRGB,
    R16F,
    RG16F,
    RGBA16F,
    R32F,
    RG32F,
    RGB32F,
    RGBA32F,
    D16,
    D24,
    D32,
    D24S8,
    D32S8,
    BC1,
    BC3,
    BC5,
    BC7,
};

enum class BufferUsage : std::uint32_t {
    Vertex  = 1u << 0,
    Index   = 1u << 1,
    Uniform = 1u << 2,
    Storage = 1u << 3,
    Transfer = 1u << 4,
};

inline BufferUsage operator|(BufferUsage a, BufferUsage b) {
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline BufferUsage& operator|=(BufferUsage& a, BufferUsage b) {
    a = a | b;
    return a;
}

inline bool HasBufferUsage(BufferUsage mask, BufferUsage bit) {
    return (static_cast<std::uint32_t>(mask) & static_cast<std::uint32_t>(bit)) != 0;
}

enum class TextureUsage : std::uint32_t {
    Sampled         = 1u << 0,
    Storage         = 1u << 1,
    ColorAttachment = 1u << 2,
    DepthAttachment = 1u << 3,
    Transfer        = 1u << 4,
};

inline TextureUsage operator|(TextureUsage a, TextureUsage b) {
    return static_cast<TextureUsage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

inline TextureUsage& operator|=(TextureUsage& a, TextureUsage b) {
    a = a | b;
    return a;
}

inline bool HasTextureUsage(TextureUsage mask, TextureUsage bit) {
    return (static_cast<std::uint32_t>(mask) & static_cast<std::uint32_t>(bit)) != 0;
}

// =============================================================================
// 资源描述符结构
// =============================================================================

struct BufferDesc {
    std::size_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
    bool cpuVisible = false;
};

struct TextureDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    Format format = Format::Undefined;
    TextureUsage usage = TextureUsage::Sampled;
    bool isCube = false;
};

// =============================================================================
// 着色器与管线相关枚举
// =============================================================================

enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEvaluation,
};

enum class PrimitiveTopology {
    TriangleList,
    TriangleStrip,
    LineList,
    PointList,
};

enum class CompareOp {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
};

enum class BlendOp {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
};

// =============================================================================
// 着色器描述
// =============================================================================

struct ShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    std::vector<std::uint8_t> code;  // SPIR-V or GLSL
    std::string entryPoint = "main";
};

// =============================================================================
// 描述符布局
// =============================================================================

enum class DescriptorType {
    UniformBuffer,
    SampledImage,
    Sampler,
    CombinedImageSampler,  /** 材质纹理槽：image + sampler 一体，供 WriteDescriptorSetTexture 使用 */
    StorageBuffer,
    StorageImage,
};

struct DescriptorBinding {
    std::uint32_t binding = 0;
    DescriptorType type = DescriptorType::UniformBuffer;
    ShaderStage visibility = ShaderStage::Vertex;
    std::uint32_t count = 1;
};

struct DescriptorSetLayoutDesc {
    std::vector<DescriptorBinding> bindings;
};

// =============================================================================
// 顶点输入
// =============================================================================

struct VertexInputBinding {
    std::uint32_t binding = 0;
    std::uint32_t stride = 0;
    bool perInstance = false;
};

struct VertexInputAttribute {
    std::uint32_t location = 0;
    std::uint32_t binding = 0;
    Format format = Format::Undefined;
    std::uint32_t offset = 0;
};

// =============================================================================
// 管线状态
// =============================================================================

struct BlendState {
    bool blendEnable = false;
    BlendFactor srcColorBlendFactor = BlendFactor::One;
    BlendFactor dstColorBlendFactor = BlendFactor::Zero;
    BlendOp colorBlendOp = BlendOp::Add;
    BlendFactor srcAlphaBlendFactor = BlendFactor::One;
    BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
    BlendOp alphaBlendOp = BlendOp::Add;
};

struct DepthStencilState {
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    CompareOp depthCompareOp = CompareOp::Less;
    bool stencilTestEnable = false;
};

struct RasterizationState {
    bool cullEnable = true;
    bool frontFaceCCW = true;
    float lineWidth = 1.0f;
};

// =============================================================================
// 管线描述
// =============================================================================

struct PipelineDesc {
    std::vector<ShaderHandle> shaders;
    std::vector<VertexInputBinding> vertexBindings;
    std::vector<VertexInputAttribute> vertexAttributes;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    RasterizationState rasterization;
    DepthStencilState depthStencil;
    std::vector<BlendState> blendStates;
    std::vector<Format> colorAttachmentFormats;
    Format depthAttachmentFormat = Format::Undefined;
};

}  // namespace kale_device
