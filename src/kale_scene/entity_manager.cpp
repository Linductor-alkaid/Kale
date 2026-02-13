/**
 * @file entity_manager.cpp
 * @brief EntityManager 非模板实现：CreateEntity/DestroyEntity/IsAlive/Update/BuildSystemOrder/RemoveComponent
 */

#include <kale_scene/entity_manager.hpp>

#include <algorithm>
#include <queue>
#include <typeindex>
#include <unordered_map>

namespace kale::scene {

EntityManager::EntityManager(
    kale::executor::RenderTaskScheduler* scheduler,
    SceneManager* sceneMgr)
    : scheduler_(scheduler), sceneMgr_(sceneMgr) {}

Entity EntityManager::CreateEntity() {
    std::uint32_t id;
    std::uint32_t gen;
    if (!freeList_.empty()) {
        id = freeList_.back();
        freeList_.pop_back();
        freeSet_.erase(id);
        gen = generations_[id];
    } else {
        id = nextId_++;
        if (id >= generations_.size())
            generations_.resize(id + 1);
        generations_[id] = 0;
        gen = 0;
    }
    Entity e{id, gen};
    for (auto& s : systems_) {
        if (s) s->OnEntityCreated(e);
    }
    return e;
}

void EntityManager::DestroyEntity(Entity entity) {
    if (!entity.IsValid() || !IsAlive(entity))
        return;
    for (auto& kv : storages_)
        kv.second->Remove(entity);
    for (auto& s : systems_) {
        if (s) s->OnEntityDestroyed(entity);
    }
    freeList_.push_back(entity.id);
    freeSet_.insert(entity.id);
    generations_[entity.id]++;
}

bool EntityManager::IsAlive(Entity entity) const {
    if (!entity.IsValid())
        return false;
    if (entity.id >= generations_.size())
        return false;
    if (entity.generation != generations_[entity.id])
        return false;
    return freeSet_.find(entity.id) == freeSet_.end();
}

void EntityManager::RemoveComponent(Entity entity, std::type_index type) {
    auto it = storages_.find(type);
    if (it != storages_.end())
        it->second->Remove(entity);
}

void EntityManager::RegisterSystem(std::unique_ptr<System> system) {
    if (system)
        systems_.push_back(std::move(system));
}

std::vector<size_t> EntityManager::BuildSystemOrder() const {
    const size_t n = systems_.size();
    if (n == 0) return {};
    std::vector<std::type_index> systemType;
    systemType.reserve(n);
    for (size_t i = 0; i < n; ++i)
        systemType.push_back(std::type_index(typeid(*systems_[i].get())));

    std::unordered_map<std::type_index, size_t> typeToIndex;
    for (size_t i = 0; i < n; ++i)
        typeToIndex[systemType[i]] = i;

    std::vector<std::vector<size_t>> dependencies(n);
    for (size_t i = 0; i < n; ++i) {
        for (const auto& dep : systems_[i]->GetDependencies()) {
            auto it = typeToIndex.find(dep);
            if (it != typeToIndex.end())
                dependencies[i].push_back(it->second);
        }
    }

    std::vector<int> in_degree(n, 0);
    for (size_t i = 0; i < n; ++i)
        in_degree[i] = static_cast<int>(dependencies[i].size());

    std::queue<size_t> q;
    for (size_t i = 0; i < n; ++i)
        if (in_degree[i] == 0)
            q.push(i);

    std::vector<size_t> order;
    order.reserve(n);
    while (!q.empty()) {
        size_t u = q.front();
        q.pop();
        order.push_back(u);
        for (size_t v = 0; v < n; ++v) {
            for (size_t d : dependencies[v]) {
                if (d == u) {
                    in_degree[v]--;
                    if (in_degree[v] == 0)
                        q.push(v);
                    break;
                }
            }
        }
    }
    return order;
}

void EntityManager::Update(float deltaTime) {
    std::vector<size_t> order = BuildSystemOrder();
    if (order.empty()) return;

    if (!scheduler_) {
        for (size_t idx : order) {
            System* s = systems_[idx].get();
            if (s)
                s->Update(deltaTime, *this);
        }
        return;
    }

    const size_t n = systems_.size();
    std::unordered_map<std::type_index, size_t> typeToIndex;
    for (size_t i = 0; i < n; ++i)
        typeToIndex[std::type_index(typeid(*systems_[i].get()))] = i;

    std::unordered_map<size_t, std::shared_future<void>> futureByIndex;
    for (size_t idx : order) {
        System* s = systems_[idx].get();
        if (!s) continue;

        std::vector<std::shared_future<void>> deps;
        for (const auto& depType : s->GetDependencies()) {
            auto it = typeToIndex.find(depType);
            if (it != typeToIndex.end()) {
                auto fit = futureByIndex.find(it->second);
                if (fit != futureByIndex.end() && fit->second.valid())
                    deps.push_back(fit->second);
            }
        }

        std::shared_future<void> f = scheduler_->SubmitRenderTask(
            [this, s, deltaTime]() { s->Update(deltaTime, *this); },
            std::move(deps));
        futureByIndex[idx] = std::move(f);
    }

    scheduler_->WaitAll();
}

}  // namespace kale::scene
