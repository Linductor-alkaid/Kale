/**
 * @file resource_types.hpp
 * @brief 资源管理层数据类型：Mesh、Texture、Material、BoundingBox
 *
 * 与 resource_management_layer_design.md 附录 A 对齐。
 * phase3-3.6：完整定义 Mesh、SubMesh、Texture、BoundingBox。
 * phase5-5.7：BoundingBox::Transform 与 TransformBounds 数学工具。
 */

#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <kale_device/rdi_types.hpp>

namespace kale::resource {

// =============================================================================
// BoundingBox
// =============================================================================

/** 轴对齐包围盒，min/max 为两个对角顶点（glm::vec3） */
struct BoundingBox {
    glm::vec3 min{0.f, 0.f, 0.f};
    glm::vec3 max{0.f, 0.f, 0.f};

    /** 使用矩阵 m 变换包围体，返回新的轴对齐包围盒（变换 8 个顶点后取 min/max） */
    BoundingBox Transform(const glm::mat4& m) const {
        glm::vec3 corners[8] = {
            {min.x, min.y, min.z}, {max.x, min.y, min.z}, {max.x, max.y, min.z}, {min.x, max.y, min.z},
            {min.x, min.y, max.z}, {max.x, min.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, max.z}
        };
        glm::vec3 tmin(std::numeric_limits<float>::max());
        glm::vec3 tmax(std::numeric_limits<float>::lowest());
        for (const auto& c : corners) {
            glm::vec4 t = m * glm::vec4(c, 1.f);
            glm::vec3 p(t.x, t.y, t.z);
            tmin = glm::min(tmin, p);
            tmax = glm::max(tmax, p);
        }
        return BoundingBox{tmin, tmax};
    }
};

/** 自由函数：使用矩阵 m 变换包围盒，返回新的轴对齐包围盒 */
inline BoundingBox TransformBounds(const BoundingBox& box, const glm::mat4& m) {
    return box.Transform(m);
}

// =============================================================================
// SubMesh（子网格，用于 LOD / 材质分组）
// =============================================================================

struct SubMesh {
    std::uint32_t indexOffset = 0;
    std::uint32_t indexCount  = 0;
    std::uint32_t materialIndex = 0;
};

// =============================================================================
// Mesh
// =============================================================================

/** 网格资源：顶点/索引缓冲、拓扑、包围盒、子网格 */
struct Mesh {
    kale_device::BufferHandle vertexBuffer{};
    kale_device::BufferHandle indexBuffer{};
    std::uint32_t indexCount  = 0;
    std::uint32_t vertexCount = 0;
    kale_device::PrimitiveTopology topology = kale_device::PrimitiveTopology::TriangleList;
    BoundingBox bounds{};
    std::vector<SubMesh> subMeshes;
};

// =============================================================================
// Texture
// =============================================================================

/** 纹理资源：RDI 句柄与元数据 */
struct Texture {
    kale_device::TextureHandle handle{};
    std::uint32_t width      = 0;
    std::uint32_t height     = 0;
    kale_device::Format format = kale_device::Format::Undefined;
    std::uint32_t mipLevels  = 1;
};

// =============================================================================
// Material（占位：后续 phase 补充材质参数与纹理引用）
// =============================================================================

struct Material {};

}  // namespace kale::resource
