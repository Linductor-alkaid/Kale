/**
 * @file component_storage.hpp
 * @brief ECS 组件存储模板
 *
 * 与 scene_management_layer_design.md 5.8 对齐。
 * phase7-7.2：ComponentStorage<T>，Add/Remove/Get/Has，entityToIndex_ 映射。
 */

#pragma once

#include <kale_scene/entity.hpp>

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace kale::scene {

/**
 * 单类型组件存储：按实体索引的稠密数组，支持 O(1) Add/Remove/Get/Has。
 * Remove 时与末尾交换以保持稠密，便于迭代。
 */
template<typename T>
class ComponentStorage {
public:
    /** 添加或覆盖实体 component */
    void Add(Entity entity, const T& component) {
        if (!entity.IsValid()) return;
        auto it = entityToIndex_.find(entity.id);
        if (it != entityToIndex_.end()) {
            components_[it->second] = component;
            return;
        }
        std::size_t i = components_.size();
        components_.push_back(component);
        indexToEntity_.push_back(entity.id);
        entityToIndex_[entity.id] = i;
    }

    /** 添加或覆盖（移动语义） */
    void Add(Entity entity, T&& component) {
        if (!entity.IsValid()) return;
        auto it = entityToIndex_.find(entity.id);
        if (it != entityToIndex_.end()) {
            components_[it->second] = std::move(component);
            return;
        }
        std::size_t i = components_.size();
        components_.push_back(std::move(component));
        indexToEntity_.push_back(entity.id);
        entityToIndex_[entity.id] = i;
    }

    /** 移除实体的该组件；若不存在则无操作 */
    void Remove(Entity entity) {
        if (!entity.IsValid()) return;
        auto it = entityToIndex_.find(entity.id);
        if (it == entityToIndex_.end()) return;
        std::size_t i = it->second;
        std::size_t last = components_.size() - 1;
        if (i != last) {
            components_[i] = std::move(components_[last]);
            std::uint32_t movedId = indexToEntity_[last];
            indexToEntity_[i] = movedId;
            entityToIndex_[movedId] = i;
        }
        entityToIndex_.erase(it);
        components_.pop_back();
        indexToEntity_.pop_back();
    }

    /** 获取组件，调用前需 Has(entity)；否则未定义行为 */
    T& Get(Entity entity) {
        return components_[entityToIndex_.at(entity.id)];
    }

    /** 只读获取 */
    const T& Get(Entity entity) const {
        return components_[entityToIndex_.at(entity.id)];
    }

    /** 实体是否拥有该组件 */
    bool Has(Entity entity) const {
        return entity.IsValid() && entityToIndex_.count(entity.id) != 0;
    }

    /** 当前存储的组件数量 */
    std::size_t Size() const { return components_.size(); }

    /** 只读迭代：按存储顺序遍历所有组件（不暴露实体，需配合索引使用） */
    const std::vector<T>& Components() const { return components_; }
    std::vector<T>& Components() { return components_; }

    /** 只读迭代：索引 i 对应的实体 id（0 <= i < Size()） */
    std::uint32_t EntityIdAt(std::size_t i) const { return indexToEntity_[i]; }

private:
    std::vector<T> components_;
    std::vector<std::uint32_t> indexToEntity_;  /**< 与 components_ 一一对应 */
    std::unordered_map<std::uint32_t, std::size_t> entityToIndex_;
};

}  // namespace kale::scene
