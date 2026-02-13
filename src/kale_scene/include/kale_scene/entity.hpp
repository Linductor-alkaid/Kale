/**
 * @file entity.hpp
 * @brief ECS 实体类型定义
 *
 * 与 scene_management_layer_design.md 5.8 对齐。
 * phase7-7.2：Entity 结构（id, generation，IsValid）、Entity::Null。
 */

#pragma once

#include <cstdint>

namespace kale::scene {

/**
 * ECS 实体句柄。
 * id 为 0 表示无效实体；generation 用于实体复用后检测悬空引用（EntityManager 层使用）。
 */
struct Entity {
    std::uint32_t id = 0;
    std::uint32_t generation = 0;

    /** 无效实体常量，用于表示未创建或已销毁的实体 */
    static constexpr Entity Null() { return Entity{0, 0}; }

    /** 是否为有效实体（id != 0） */
    bool IsValid() const { return id != 0; }

    bool operator==(const Entity& other) const {
        return id == other.id && generation == other.generation;
    }
    bool operator!=(const Entity& other) const { return !(*this == other); }
};

/** 无效实体常量（与 Entity::Null() 等价，便于书写） */
inline constexpr Entity kNullEntity{0, 0};

}  // namespace kale::scene
