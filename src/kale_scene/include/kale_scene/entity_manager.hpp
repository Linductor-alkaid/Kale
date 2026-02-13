/**
 * @file entity_manager.hpp
 * @brief ECS 实体管理器与 System 基类
 *
 * 与 scene_management_layer_design.md 5.8 对齐。
 * phase7-7.3：EntityManager（CreateEntity/DestroyEntity/IsAlive、AddComponent/GetComponent/HasComponent/RemoveComponent、EntitiesWith、RegisterSystem、Update）。
 * phase7-7.4 对齐：System 基类（Update(deltaTime, EntityManager&)、GetDependencies）。
 */

#pragma once

#include <kale_scene/entity.hpp>
#include <kale_scene/component_storage.hpp>

#include <kale_executor/render_task_scheduler.hpp>

#include <memory>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace kale::scene {

class SceneManager;
class EntityManager;

/**
 * ECS 系统基类：由 EntityManager 按依赖 DAG 调度。
 * Update(deltaTime, em) 中通过 em 查询/修改组件与场景桥接。
 */
class System {
public:
    virtual ~System() = default;
    virtual void Update(float deltaTime, EntityManager& em) = 0;
    /// 返回本系统依赖的系统类型（type_index），用于构建 DAG；默认无依赖
    virtual std::vector<std::type_index> GetDependencies() const { return {}; }
    /// 可选：实体创建时回调
    virtual void OnEntityCreated(Entity /*entity*/) {}
    /// 可选：实体销毁时回调
    virtual void OnEntityDestroyed(Entity /*entity*/) {}
};

/**
 * 类型擦除的组件存储接口：供 EntityManager 在 DestroyEntity 时统一 Remove。
 */
struct IComponentStorage {
    virtual ~IComponentStorage() = default;
    virtual void Remove(Entity entity) = 0;
    virtual bool Has(Entity entity) const = 0;
};

template <typename T>
struct StorageHolder : IComponentStorage {
    ComponentStorage<T> storage;
    void Remove(Entity entity) override { storage.Remove(entity); }
    bool Has(Entity entity) const override { return storage.Has(entity); }
};

/**
 * 实体管理器：实体生命周期、组件存储、系统注册与按依赖 DAG 更新。
 * 可选持有 RenderTaskScheduler，Update 时按拓扑序提交系统任务；无 scheduler 时主线程顺序执行。
 */
class EntityManager {
public:
    /**
     * @param scheduler 可选；非空时 Update 通过 SubmitRenderTask 按 DAG 提交系统，并 WaitAll
     * @param sceneMgr 可选；供 System 通过 SceneNodeRef 等写回场景图
     */
    explicit EntityManager(
        kale::executor::RenderTaskScheduler* scheduler = nullptr,
        SceneManager* sceneMgr = nullptr);

    void SetSceneManager(SceneManager* sceneMgr) { sceneMgr_ = sceneMgr; }
    SceneManager* GetSceneManager() const { return sceneMgr_; }

    Entity CreateEntity();
    void DestroyEntity(Entity entity);
    bool IsAlive(Entity entity) const;

    /**
     * 每帧调用：根据各 System::GetDependencies() 构建 DAG，按拓扑序执行系统。
     * 有 scheduler 时提交任务并 WaitAll；无 scheduler 时主线程顺序执行。
     */
    void Update(float deltaTime);

    template <typename T, typename... Args>
    T& AddComponent(Entity entity, Args&&... args);

    template <typename T>
    T* GetComponent(Entity entity);

    template <typename T>
    const T* GetComponent(Entity entity) const;

    template <typename T>
    bool HasComponent(Entity entity) const;

    void RemoveComponent(Entity entity, std::type_index type);

    /// 返回同时拥有 Components... 的所有实体（无序）。至少一个组件类型时有效。
    template <typename... Components>
    std::vector<Entity> EntitiesWith() const;

    void RegisterSystem(std::unique_ptr<System> system);

private:
    template <typename T>
    ComponentStorage<T>* GetOrCreateStorage();
    template <typename T>
    ComponentStorage<T>* GetStorage();
    template <typename T>
    const ComponentStorage<T>* GetStorage() const;

    /// 构建系统拓扑序（Kahn）；返回 system 下标序列
    std::vector<size_t> BuildSystemOrder() const;

    kale::executor::RenderTaskScheduler* scheduler_ = nullptr;
    SceneManager* sceneMgr_ = nullptr;

    std::vector<std::uint32_t> generations_;
    std::vector<std::uint32_t> freeList_;
    std::unordered_set<std::uint32_t> freeSet_;
    std::uint32_t nextId_ = 1;

    std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> storages_;
    std::vector<std::unique_ptr<System>> systems_;
};

// -----------------------------------------------------------------------------
// 模板实现
// -----------------------------------------------------------------------------

template <typename T>
ComponentStorage<T>* EntityManager::GetOrCreateStorage() {
    std::type_index key(typeid(T));
    auto it = storages_.find(key);
    if (it != storages_.end())
        return &static_cast<StorageHolder<T>*>(it->second.get())->storage;
    auto holder = std::make_unique<StorageHolder<T>>();
    ComponentStorage<T>* p = &holder->storage;
    storages_[key] = std::move(holder);
    return p;
}

template <typename T>
ComponentStorage<T>* EntityManager::GetStorage() {
    auto it = storages_.find(std::type_index(typeid(T)));
    if (it == storages_.end()) return nullptr;
    return &static_cast<StorageHolder<T>*>(it->second.get())->storage;
}

template <typename T>
const ComponentStorage<T>* EntityManager::GetStorage() const {
    auto it = storages_.find(std::type_index(typeid(T)));
    if (it == storages_.end()) return nullptr;
    return &static_cast<const StorageHolder<T>*>(it->second.get())->storage;
}

template <typename T, typename... Args>
T& EntityManager::AddComponent(Entity entity, Args&&... args) {
    ComponentStorage<T>* s = GetOrCreateStorage<T>();
    T obj(std::forward<Args>(args)...);
    s->Add(entity, std::move(obj));
    return s->Get(entity);
}

template <typename T>
T* EntityManager::GetComponent(Entity entity) {
    ComponentStorage<T>* s = GetStorage<T>();
    if (!s || !s->Has(entity)) return nullptr;
    return &s->Get(entity);
}

template <typename T>
const T* EntityManager::GetComponent(Entity entity) const {
    const ComponentStorage<T>* s = GetStorage<T>();
    if (!s || !s->Has(entity)) return nullptr;
    return &s->Get(entity);
}

template <typename T>
bool EntityManager::HasComponent(Entity entity) const {
    const ComponentStorage<T>* s = GetStorage<T>();
    return s && s->Has(entity);
}

template <typename... Components>
std::vector<Entity> EntityManager::EntitiesWith() const {
    std::vector<Entity> out;
    using First = std::tuple_element_t<0, std::tuple<Components...>>;
    const ComponentStorage<First>* first = GetStorage<First>();
    if (!first) return out;
    for (size_t i = 0; i < first->Size(); ++i) {
        Entity e;
        e.id = first->EntityIdAt(i);
        e.generation = (e.id < generations_.size()) ? generations_[e.id] : 0;
        if (!IsAlive(e)) continue;
        bool hasAll = (HasComponent<Components>(e) && ...);
        if (hasAll) out.push_back(e);
    }
    return out;
}

}  // namespace kale::scene
