/**
 * @file resource_cache.hpp
 * @brief 资源缓存：CacheEntry、Register/Get/FindByPath、引用计数、线程安全
 *
 * 与 resource_management_layer_design.md 5.3 对齐。
 */

#pragma once

#include <any>
#include <mutex>
#include <optional>
#include <string>
#include <typeindex>
#include <unordered_map>

#include <kale_resource/resource_handle.hpp>

namespace kale::resource {

/**
 * @brief 缓存条目：资源、路径、引用计数、就绪状态、类型
 */
struct CacheEntry {
    std::any resource;
    std::string path;
    std::uint32_t refCount = 0;
    bool isReady = false;
    std::type_index typeId{typeid(void)};
};

/**
 * @brief 资源缓存：登记、获取、路径查找、引用计数，entries_/pathToId_ 受 mutex 保护
 */
class ResourceCache {
public:
    /**
     * @brief 登记资源（refCount=1）
     * @return 新分配的句柄
     */
    template <typename T>
    ResourceHandle<T> Register(const std::string& path, T* resource, bool ready = true) {
        std::lock_guard lock(mutex_);
        const std::uint64_t id = nextId_++;
        const std::type_index ti = typeid(T);
        std::string pathKey = makePathKey(path, ti);
        entries_[id] = CacheEntry{
            std::any(resource),
            path,
            1u,
            ready,
            ti,
        };
        pathToId_[std::move(pathKey)] = id;
        return ResourceHandle<T>{id};
    }

    /**
     * @brief 预注册占位条目（refCount=1，isReady=false，resource 空）
     * 若 path+typeId 已存在则返回已有句柄
     */
    template <typename T>
    ResourceHandle<T> RegisterPlaceholder(const std::string& path) {
        std::lock_guard lock(mutex_);
        const std::type_index ti = typeid(T);
        std::string pathKey = makePathKey(path, ti);
        auto it = pathToId_.find(pathKey);
        if (it != pathToId_.end()) {
            return ResourceHandle<T>{it->second};
        }
        const std::uint64_t id = nextId_++;
        entries_[id] = CacheEntry{
            std::any(),
            path,
            1u,
            false,
            ti,
        };
        pathToId_[std::move(pathKey)] = id;
        return ResourceHandle<T>{id};
    }

    /**
     * @brief 获取资源指针；无条目或 resource 为空则返回 nullptr
     */
    template <typename T>
    T* Get(ResourceHandle<T> handle) {
        if (!handle.IsValid()) return nullptr;
        std::lock_guard lock(mutex_);
        auto it = entries_.find(handle.id);
        if (it == entries_.end()) return nullptr;
        std::any& a = it->second.resource;
        if (!a.has_value()) return nullptr;
        T* const* pp = std::any_cast<T*>(&a);
        return pp ? *pp : nullptr;
    }

    /**
     * @brief 检查资源是否就绪
     */
    template <typename T>
    bool IsReady(ResourceHandle<T> handle) const {
        if (!handle.IsValid()) return false;
        std::lock_guard lock(mutex_);
        auto it = entries_.find(handle.id);
        if (it == entries_.end()) return false;
        return it->second.isReady;
    }

    /**
     * @brief 设置条目资源（用于异步加载完成后写入）
     */
    void SetResource(ResourceHandleAny handle, std::any resource) {
        if (!handle.IsValid()) return;
        std::lock_guard lock(mutex_);
        auto it = entries_.find(handle.id);
        if (it != entries_.end()) {
            it->second.resource = std::move(resource);
        }
    }

    /**
     * @brief 标记条目就绪
     */
    void SetReady(ResourceHandleAny handle) {
        if (!handle.IsValid()) return;
        std::lock_guard lock(mutex_);
        auto it = entries_.find(handle.id);
        if (it != entries_.end()) {
            it->second.isReady = true;
        }
    }

    /**
     * @brief 增加引用计数
     */
    void AddRef(ResourceHandleAny handle) {
        if (!handle.IsValid()) return;
        std::lock_guard lock(mutex_);
        auto it = entries_.find(handle.id);
        if (it != entries_.end() && it->second.typeId == handle.typeId) {
            ++it->second.refCount;
        }
    }

    /**
     * @brief 减少引用计数；为 0 时移除条目并清理 pathToId_
     */
    void Release(ResourceHandleAny handle) {
        if (!handle.IsValid()) return;
        std::lock_guard lock(mutex_);
        auto it = entries_.find(handle.id);
        if (it == entries_.end() || it->second.typeId != handle.typeId) return;
        if (it->second.refCount > 0) {
            --it->second.refCount;
        }
        if (it->second.refCount == 0) {
            const std::string pathKey = makePathKey(it->second.path, it->second.typeId);
            pathToId_.erase(pathKey);
            entries_.erase(it);
        }
    }

    std::uint64_t GetId(ResourceHandleAny handle) const { return handle.id; }

    /**
     * @brief 按路径与类型查找句柄
     */
    std::optional<ResourceHandleAny> FindByPath(const std::string& path,
                                                std::type_index typeId) const {
        std::lock_guard lock(mutex_);
        std::string pathKey = makePathKey(path, typeId);
        auto it = pathToId_.find(pathKey);
        if (it == pathToId_.end()) return std::nullopt;
        return ResourceHandleAny{it->second, typeId};
    }

private:
    static std::string makePathKey(const std::string& path, std::type_index typeId) {
        return path + '\0' + typeId.name();
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, CacheEntry> entries_;
    std::unordered_map<std::string, std::uint64_t> pathToId_;
    std::uint64_t nextId_ = 1;
};

}  // namespace kale::resource
