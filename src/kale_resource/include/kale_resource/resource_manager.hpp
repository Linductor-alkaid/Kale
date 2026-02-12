/**
 * @file resource_manager.hpp
 * @brief 资源管理器：Loader 注册、路径解析、缓存访问
 *
 * 与 resource_management_layer_design.md 5.2、6.1 对齐。
 * phase3-3.4：构造函数、RegisterLoader、FindLoader、SetAssetPath、AddPathAlias、ResolvePath。
 */

#pragma once

#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include <kale_device/render_device.hpp>
#include <kale_resource/resource_cache.hpp>
#include <kale_resource/resource_loader.hpp>

namespace kale::resource {

class StagingMemoryManager;

/**
 * @brief 渲染任务调度器前向声明（与 executor 层协作，LoadAsync 时使用）
 * 具体类型由上层或 executor 层定义。
 */
struct RenderTaskScheduler;

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
     * @param scheduler 任务调度器（LoadAsync 用，可为 nullptr）
     * @param device 渲染设备（Loader 创建 GPU 资源）
     * @param stagingMgr 暂存内存管理（上传用，可为 nullptr）
     */
    explicit ResourceManager(RenderTaskScheduler* scheduler,
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
     * @brief 获取最后一次错误信息（如 Load 失败原因）
     */
    std::string GetLastError() const;

    /**
     * @brief 设置最后一次错误信息（供 Loader 或 Load 内部使用）
     */
    void SetLastError(const std::string& message);

    /**
     * @brief 访问内部缓存（供 Load/LoadAsync/Get 等使用）
     */
    ResourceCache& GetCache() { return cache_; }
    const ResourceCache& GetCache() const { return cache_; }

    kale_device::IRenderDevice* GetDevice() const { return device_; }
    StagingMemoryManager* GetStagingMgr() const { return stagingMgr_; }
    RenderTaskScheduler* GetScheduler() const { return scheduler_; }

private:
    ResourceCache cache_;
    std::vector<std::unique_ptr<IResourceLoader>> loaders_;
    RenderTaskScheduler* scheduler_ = nullptr;
    kale_device::IRenderDevice* device_ = nullptr;
    StagingMemoryManager* stagingMgr_ = nullptr;
    std::string assetPath_;
    std::unordered_map<std::string, std::string> pathAliases_;
    mutable std::string lastError_;
};

}  // namespace kale::resource
