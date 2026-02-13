/**
 * @file resource_manager.hpp
 * @brief 资源管理器：Loader 注册、路径解析、缓存访问
 *
 * 与 resource_management_layer_design.md 5.2、6.1 对齐。
 * phase3-3.4：构造函数、RegisterLoader、FindLoader、SetAssetPath、AddPathAlias、ResolvePath。
 */

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>
#include <stdexcept>
#include <optional>

#include <kale_device/render_device.hpp>
#include <kale_executor/executor_future.hpp>
#include <kale_executor/render_task_scheduler.hpp>
#include <kale_resource/resource_cache.hpp>
#include <kale_resource/resource_handle.hpp>
#include <kale_resource/resource_loader.hpp>
#include <kale_resource/resource_types.hpp>

namespace kale::resource {

class StagingMemoryManager;

/**
 * @brief 资源管理器：统一入口，Loader 注册、路径解析、缓存委托
 *
 * 构造时注入 scheduler（可为 nullptr，异步加载未实现时）、device、stagingMgr（可为 nullptr）。
 * 路径解析：SetAssetPath 设置资源根路径，AddPathAlias 设置别名，ResolvePath 得到完整路径。
 */
class ResourceManager {
public:
    /**
     * @brief 构造
     * @param scheduler 任务调度器（LoadAsync 用，可为 nullptr；为 null 时 LoadAsync 退化为同步加载并返回就绪 Future）
     * @param device 渲染设备（Loader 创建 GPU 资源）
     * @param stagingMgr 暂存内存管理（上传用，可为 nullptr）
     */
    explicit ResourceManager(kale::executor::RenderTaskScheduler* scheduler,
                             kale_device::IRenderDevice* device,
                             StagingMemoryManager* stagingMgr = nullptr);

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ~ResourceManager() = default;

    /**
     * @brief 注册一个加载器（按注册顺序查找，先注册优先）
     */
    void RegisterLoader(std::unique_ptr<IResourceLoader> loader);

    /**
     * @brief 根据路径与资源类型查找可用的 Loader
     * @return 支持该 path 且资源类型匹配的第一个 Loader，否则 nullptr
     */
    IResourceLoader* FindLoader(const std::string& path, std::type_index typeId);

    /**
     * @brief 设置资源根路径（相对路径将与此拼接）
     */
    void SetAssetPath(const std::string& path);

    /**
     * @brief 添加路径别名（例如 alias="@models", path="assets/models"）
     * 解析时若路径以 alias 开头则替换为 path
     */
    void AddPathAlias(const std::string& alias, const std::string& path);

    /**
     * @brief 解析为完整路径：先应用别名，再对相对路径拼接 assetPath_
     */
    std::string ResolvePath(const std::string& path) const;

    /**
     * @brief 同步加载资源
     * @tparam T 资源类型（Mesh、Texture、Material 等）
     * @param path 资源路径（相对 assetPath 或带别名）
     * @return 成功返回有效句柄并已登记入缓存（若已存在则 AddRef 后返回）；失败返回空句柄，GetLastError() 返回原因；失败不缓存
     */
    template <typename T>
    ResourceHandle<T> Load(const std::string& path);

    /**
     * @brief 异步加载资源（不阻塞主线程）
     * @tparam T 资源类型（Mesh、Texture、Material 等）
     * @param path 资源路径（相对 assetPath 或带别名）
     * @return ExecutorFuture<ResourceHandle<T>>；若已缓存则返回已就绪的 Future；无 scheduler 时退化为同步加载后返回就绪 Future；失败时 Future.get() 返回空句柄
     */
    template <typename T>
    kale::executor::ExecutorFuture<ResourceHandle<T>> LoadAsync(const std::string& path);

    /**
     * @brief 获取最后一次错误信息（如 Load 失败原因）
     */
    std::string GetLastError() const;

    /**
     * @brief 设置最后一次错误信息（供 Loader 或 Load 内部使用）
     */
    void SetLastError(const std::string& message);

    /**
     * @brief 加载完成回调类型：void(ResourceHandleAny handle, const std::string& path)
     * 供主循环在 ProcessLoadedCallbacks() 中派发。
     */
    using LoadedCallback = std::function<void(ResourceHandleAny, const std::string&)>;

    /**
     * @brief 注册加载完成回调（可选）；ProcessLoadedCallbacks() 会按注册顺序调用
     */
    void RegisterLoadedCallback(LoadedCallback cb);

    /**
     * @brief 处理本帧已完成的加载，派发所有已注册的 LoadedCallback（供主循环每帧调用）
     */
    void ProcessLoadedCallbacks();

    /**
     * @brief 创建占位符资源（简单几何体、1x1 默认纹理、默认材质）；需在 SetAssetPath 后、使用 GetPlaceholder* 前调用；无 device 时跳过
     */
    void CreatePlaceholders();

    /**
     * @brief 检查资源是否就绪（供上层 Draw 时检查）
     */
    template <typename T>
    bool IsReady(ResourceHandle<T> handle) const;

    /**
     * @brief 获取资源指针；未就绪时返回 nullptr（phase4-4.6）
     */
    template <typename T>
    T* Get(ResourceHandle<T> handle);

    /**
     * @brief 按路径获取或创建占位条目；返回 (句柄, 是否新创建)。新创建时需由调用方触发 LoadAsync 一次，避免重复触发
     */
    template <typename T>
    std::pair<ResourceHandle<T>, bool> GetOrCreatePlaceholder(const std::string& path);

    /**
     * @brief 获取占位符 Mesh（未就绪时 Draw 使用）；CreatePlaceholders 未调用或失败时返回 nullptr
     */
    Mesh* GetPlaceholderMesh();
    /**
     * @brief 获取占位符 Texture；CreatePlaceholders 未调用或失败时返回 nullptr
     */
    Texture* GetPlaceholderTexture();
    /**
     * @brief 获取占位符 Material；CreatePlaceholders 未调用或失败时返回 nullptr
     */
    Material* GetPlaceholderMaterial();

    /**
     * @brief 访问内部缓存（供 Load/LoadAsync/Get 等使用）
     */
    ResourceCache& GetCache() { return cache_; }
    const ResourceCache& GetCache() const { return cache_; }

    kale_device::IRenderDevice* GetDevice() const { return device_; }
    StagingMemoryManager* GetStagingMgr() const { return stagingMgr_; }
    kale::executor::RenderTaskScheduler* GetScheduler() const { return scheduler_; }

private:
    /// 供 LoadAsync 任务内成功完成时入队，由 ProcessLoadedCallbacks 派发
    void EnqueueLoaded(ResourceHandleAny handle, const std::string& path);

    ResourceCache cache_;
    std::vector<std::unique_ptr<IResourceLoader>> loaders_;
    kale::executor::RenderTaskScheduler* scheduler_ = nullptr;
    kale_device::IRenderDevice* device_ = nullptr;
    StagingMemoryManager* stagingMgr_ = nullptr;
    std::string assetPath_;
    std::unordered_map<std::string, std::string> pathAliases_;
    mutable std::string lastError_;

    std::mutex loadedMutex_;
    std::vector<std::pair<ResourceHandleAny, std::string>> pendingLoaded_;
    std::vector<LoadedCallback> loadedCallbacks_;

    std::unique_ptr<Mesh> placeholderMesh_;
    std::unique_ptr<Texture> placeholderTexture_;
    std::unique_ptr<Material> placeholderMaterial_;
};

// -----------------------------------------------------------------------------
// 同步 Load 模板实现（phase3-3.5）
// -----------------------------------------------------------------------------
template <typename T>
ResourceHandle<T> ResourceManager::Load(const std::string& path) {
    const std::string resolved = ResolvePath(path);
    const std::type_index typeId = typeid(T);

    // 检查缓存：若已存在则 AddRef 并返回
    if (auto existing = cache_.FindByPath(resolved, typeId)) {
        cache_.AddRef(*existing);
        return ResourceHandle<T>{existing->id};
    }

    // 查找 Loader
    IResourceLoader* loader = FindLoader(resolved, typeId);
    if (!loader) {
        SetLastError("No loader found for path: " + path);
        return ResourceHandle<T>{};
    }

    // 调用 Loader 同步加载
    ResourceLoadContext ctx{device_, stagingMgr_, this};
    std::any result = loader->Load(resolved, ctx);

    if (!result.has_value()) {
        if (GetLastError().empty()) {
            SetLastError("Load failed: " + path);
        }
        return ResourceHandle<T>{};
    }

    // 从 std::any 提取 T*：支持 unique_ptr<T> 或 T*
    T* ptr = nullptr;
    try {
        if (auto* u = std::any_cast<std::unique_ptr<T>>(&result)) {
            ptr = u->release();
        } else if (auto* p = std::any_cast<T*>(&result)) {
            ptr = *p;
        }
    } catch (const std::bad_any_cast&) {
        SetLastError("Loader returned invalid type for: " + path);
        return ResourceHandle<T>{};
    }

    if (!ptr) {
        SetLastError("Loader returned null for: " + path);
        return ResourceHandle<T>{};
    }

    return cache_.Register<T>(resolved, ptr, true);
}

// -----------------------------------------------------------------------------
// LoadAsync 模板实现（phase4-4.3）
// -----------------------------------------------------------------------------
template <typename T>
kale::executor::ExecutorFuture<ResourceHandle<T>> ResourceManager::LoadAsync(
    const std::string& path) {
    const std::string resolved = ResolvePath(path);
    const std::type_index typeId = typeid(T);

    // 检查缓存：若已存在且就绪则 AddRef 并返回已就绪的 Future
    if (auto existing = cache_.FindByPath(resolved, typeId)) {
        ResourceHandle<T> handle{existing->id};
        if (cache_.IsReady(handle)) {
            cache_.AddRef(*existing);
            kale::executor::ExecutorPromise<ResourceHandle<T>> p;
            p.set_value(handle);
            return p.get_future();
        }
        // 占位符已存在，下面 RegisterPlaceholder 会返回同一 handle
    }

    // 预注册占位条目（若已存在则返回已有句柄）
    ResourceHandle<T> handle = cache_.RegisterPlaceholder<T>(resolved);

    auto load_task = [this, resolved, typeId, handle]() -> ResourceHandle<T> {
        if (cache_.IsReady(handle))
            return handle;
        IResourceLoader* loader = FindLoader(resolved, typeId);
        if (!loader) {
            std::string err("No loader found for path: " + resolved);
            SetLastError(err);
            cache_.Release(ToAny(handle));
            throw std::runtime_error(err);
        }
        ResourceLoadContext ctx{device_, stagingMgr_, this};
        std::any result = loader->Load(resolved, ctx);
        if (!result.has_value()) {
            std::string err(GetLastError().empty() ? "Load failed: " + resolved : GetLastError());
            SetLastError(err);
            cache_.Release(ToAny(handle));
            throw std::runtime_error(err);
        }
        T* ptr = nullptr;
        try {
            if (auto* u = std::any_cast<std::unique_ptr<T>>(&result))
                ptr = u->release();
            else if (auto* p = std::any_cast<T*>(&result))
                ptr = *p;
        } catch (const std::bad_any_cast&) {
            std::string err("Loader returned invalid type for: " + resolved);
            SetLastError(err);
            cache_.Release(ToAny(handle));
            throw std::runtime_error(err);
        }
        if (!ptr) {
            std::string err("Loader returned null for: " + resolved);
            SetLastError(err);
            cache_.Release(ToAny(handle));
            throw std::runtime_error(err);
        }
        cache_.SetResource(ToAny(handle), std::any(ptr));
        cache_.SetReady(ToAny(handle));
        EnqueueLoaded(ToAny(handle), resolved);
        return handle;
    };

    if (scheduler_) {
        return scheduler_->LoadResourceAsync<ResourceHandle<T>>(std::move(load_task));
    }
    // 无 scheduler：同步执行后返回就绪 Future；失败时通过 promise 传递异常
    kale::executor::ExecutorPromise<ResourceHandle<T>> p;
    kale::executor::ExecutorFuture<ResourceHandle<T>> f = p.get_future();
    try {
        ResourceHandle<T> result = load_task();
        p.set_value(result);
    } catch (...) {
        p.set_exception(std::current_exception());
    }
    return f;
}

// -----------------------------------------------------------------------------
// IsReady / Get / GetOrCreatePlaceholder（phase4-4.6）
// -----------------------------------------------------------------------------
template <typename T>
bool ResourceManager::IsReady(ResourceHandle<T> handle) const {
    return cache_.IsReady(handle);
}

template <typename T>
T* ResourceManager::Get(ResourceHandle<T> handle) {
    if (!handle.IsValid()) return nullptr;
    if (!cache_.IsReady(handle)) return nullptr;
    return cache_.Get(handle);
}

template <typename T>
std::pair<ResourceHandle<T>, bool> ResourceManager::GetOrCreatePlaceholder(
    const std::string& path) {
    const std::string resolved = ResolvePath(path);
    const std::type_index typeId = typeid(T);
    auto existing = cache_.FindByPath(resolved, typeId);
    if (existing) {
        return {ResourceHandle<T>{existing->id}, false};
    }
    ResourceHandle<T> handle = cache_.RegisterPlaceholder<T>(resolved);
    return {handle, true};
}

}  // namespace kale::resource
