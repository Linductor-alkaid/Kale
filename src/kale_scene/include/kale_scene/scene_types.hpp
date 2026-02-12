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

}  // namespace kale::scene
