/**
 * @file scene_types.hpp
 * @brief 场景管理层句柄与类型定义
 *
 * 与 scene_management_layer_design.md 5.1 对齐。
 * phase5-5.1：SceneNodeHandle、kInvalidSceneNodeHandle。
 */

#pragma once

#include <cstdint>

namespace kale::scene {

/** 场景节点句柄，用于类型安全引用场景图节点 */
using SceneNodeHandle = std::uint64_t;

/** 无效句柄常量，用于表示未绑定或已销毁的节点 */
constexpr SceneNodeHandle kInvalidSceneNodeHandle = 0;

/**
 * 渲染 Pass 标志，用于 CullScene 与 Render Graph 按 Pass 过滤节点。
 * 与 scene_management_layer_design.md 5.2 对齐。
 */
enum class PassFlags : std::uint32_t {
    ShadowCaster = 1u,   ///< 参与 Shadow Pass
    Opaque       = 2u,   ///< 参与 GBuffer/不透明几何体 Pass
    Transparent  = 4u,   ///< 参与透明 Pass
    All          = ShadowCaster | Opaque | Transparent,
};

/** 便于按位与判断： (node->GetPassFlags() & pass) != 0 */
inline PassFlags operator|(PassFlags a, PassFlags b) {
    return static_cast<PassFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline PassFlags operator&(PassFlags a, PassFlags b) {
    return static_cast<PassFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

}  // namespace kale::scene
