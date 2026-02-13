/**
 * @file resource_cache.hpp
 * @brief 资源缓存：CacheEntry、Register/Get/FindByPath、引用计数、线程安全
 *
 * 与 resource_management_layer_design.md 5.3 对齐。
 */

#pragma once

#include <any>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <kale_resource/resource_handle.hpp>

namespace kale::resource {

/**
 * @brief 缓存条目：资源、路径、引用计数、就绪状态、类型
 * @note pendingRelease 为 true 表示已加入待释放队列，等待 ProcessPendingReleases 统一销毁
 */
struct CacheEntry {
    std::any resource;
    std::string path;
    std::uint32_t refCount = 0;
    bool isReady = false;
    std::type_index typeId{typeid(void)};
    bool pendingRelease = false;
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
     * @brief 取出条目中的资源（用于热重载替换前销毁旧资源）；条目保留，resource 置空、isReady 置 false
     * @return 原 resource 的 std::any；无条目或 handle 不匹配时返回空 std::any
     */
    std::any TakeResource(ResourceHandleAny handle) {
        if (!handle.IsValid()) return std::any();
        std::lock_guard lock(mutex_);
        auto it = entries_.find(handle.id);
        if (it == entries_.end() || it->second.typeId != handle.typeId)
            return std::any();
        std::any old = std::move(it->second.resource);
        it->second.isReady = false;
        return old;
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
     * @brief 减少引用计数；为 0 时加入待释放队列（不立即销毁，由 ProcessPendingReleases 下一帧统一销毁）
     */
    void Release(ResourceHandleAny handle) {
        if (!handle.IsValid()) return;
        std::lock_guard lock(mutex_);
        auto it = entries_.find(handle.id);
        if (it == entries_.end() || it->second.typeId != handle.typeId) return;
        if (it->second.refCount > 0) {
            --it->second.refCount;
        }
        if (it->second.refCount == 0 && !it->second.pendingRelease) {
            it->second.pendingRelease = true;
            const std::string pathKey = makePathKey(it->second.path, it->second.typeId);
            pathToId_.erase(pathKey);
            pendingReleases_.push_back(handle);
        }
    }

    /** @brief 待释放回调：(ResourceHandleAny handle, std::any& resource)，调用方负责销毁 GPU 资源并删除对象 */
    using PendingReleaseCallback = std::function<void(ResourceHandleAny, std::any&)>;

    /**
     * @brief 处理待释放队列：对每项调用 callback(handle, resource)，然后移除条目；供主循环下一帧调用
     */
    void ProcessPendingReleases(PendingReleaseCallback callback) {
        if (!callback) return;
        std::lock_guard lock(mutex_);
        for (ResourceHandleAny h : pendingReleases_) {
            auto it = entries_.find(h.id);
            if (it == entries_.end() || it->second.typeId != h.typeId) continue;
            callback(h, it->second.resource);
            entries_.erase(it);
        }
        pendingReleases_.clear();
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

    /**
     * @brief 遍历所有已加载且未待释放的条目（用于热重载侦测）
     * @param f 回调 void(const std::string& path, std::type_index typeId, ResourceHandleAny handle)
     */
    template <typename Func>
    void ForEachLoadedEntry(Func&& f) const {
        std::lock_guard lock(mutex_);
        for (const auto& [id, entry] : entries_) {
            if (entry.isReady && !entry.pendingRelease && !entry.path.empty()) {
                f(entry.path, entry.typeId, ResourceHandleAny{id, entry.typeId});
            }
        }
    }

private:
    static std::string makePathKey(const std::string& path, std::type_index typeId) {
        return path + '\0' + typeId.name();
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, CacheEntry> entries_;
    std::unordered_map<std::string, std::uint64_t> pathToId_;
    std::vector<ResourceHandleAny> pendingReleases_;
    std::uint64_t nextId_ = 1;
};

}  // namespace kale::resource
