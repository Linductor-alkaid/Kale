/**
 * @file resource_handle.hpp
 * @brief 资源管理层句柄类型：ResourceHandle<T>、ResourceHandleAny、ToAny
 *
 * 与 resource_management_layer_design.md 5.1 对齐。
 * 用于 Mesh、Texture、Material 等高层资源的句柄化引用。
 */

#pragma once

#include <cstdint>
#include <typeindex>

#include <kale_resource/resource_types.hpp>

namespace kale::resource {

/**
 * @brief 类型安全资源句柄模板
 * @tparam T 资源类型（Mesh、Texture、Material 等）
 */
template <typename T>
struct ResourceHandle {
    std::uint64_t id = 0;

    bool IsValid() const { return id != 0; }
    bool operator==(const ResourceHandle& other) const { return id == other.id; }
    bool operator!=(const ResourceHandle& other) const { return id != other.id; }
};

/**
 * @brief 类型擦除句柄，用于 Unload 等通用接口
 */
struct ResourceHandleAny {
    std::uint64_t id = 0;
    std::type_index typeId{typeid(void)};

    bool IsValid() const { return id != 0; }
    bool operator==(const ResourceHandleAny& other) const {
        return id == other.id && typeId == other.typeId;
    }
    bool operator!=(const ResourceHandleAny& other) const { return !(*this == other); }
};

/**
 * @brief 将 ResourceHandle<T> 转换为 ResourceHandleAny
 */
template <typename T>
inline ResourceHandleAny ToAny(ResourceHandle<T> h) {
    return ResourceHandleAny{h.id, std::type_index(typeid(T))};
}

// =============================================================================
// 资源句柄类型别名
// =============================================================================

using MeshHandle     = ResourceHandle<Mesh>;
using TextureHandle = ResourceHandle<Texture>;   // 命名空间 kale::resource 下，与 RDI kale_device::TextureHandle 区分
using MaterialHandle = ResourceHandle<Material>;

}  // namespace kale::resource
