/**
 * @file shader_manager.hpp
 * @brief 着色器管理器：加载、缓存、热重载，供 Render Graph / Material 使用
 *
 * 与 phase10-10.8、rendering_pipeline_layer_todolist 5.1 对齐。
 * 依赖 ShaderCompiler 编译，缓存由 path+stage 构成的 name → ShaderHandle。
 */

#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

namespace kale::resource {
class ShaderCompiler;
}

namespace kale::pipeline {

/**
 * @brief 着色器管理器
 *
 * LoadShader(path, stage, device)：经 ShaderCompiler 编译并缓存，返回 ShaderHandle。
 * GetShader(name)：按 name 查找，name 为 LoadShader 时使用的缓存键（path|Stage 或 LoadShader 返回前可通过 GetCacheKey 获得）。
 * ReloadShader(path)：对该 path 下已缓存的所有 stage 重新编译并更新缓存。
 * 缓存键格式：path + "|" + Stage 后缀（如 "mesh.vert|Vertex"），便于 Reload 按 path 匹配。
 */
class ShaderManager {
public:
    ShaderManager() = default;

    /** 设置着色器编译器（非占有），Load/Reload 时使用 */
    void SetCompiler(kale::resource::ShaderCompiler* compiler) { compiler_ = compiler; }
    kale::resource::ShaderCompiler* GetCompiler() const { return compiler_; }

    /** 设置渲染设备（非占有），ReloadShader 需用于 DestroyShader 与 Recompile */
    void SetDevice(kale_device::IRenderDevice* device) { device_ = device; }
    kale_device::IRenderDevice* GetDevice() const { return device_; }

    /**
     * 加载并缓存着色器。
     * @param path 资源路径（经 ShaderCompiler::ResolvePath 若设置了 basePath）
     * @param stage 着色器阶段
     * @param device 渲染设备，不可为 nullptr
     * @return 有效 ShaderHandle 或无效句柄；若已缓存则返回缓存句柄
     */
    kale_device::ShaderHandle LoadShader(const std::string& path,
                                         kale_device::ShaderStage stage,
                                         kale_device::IRenderDevice* device);

    /**
     * 按 name 查找缓存句柄。name 为 MakeCacheKey(path, stage) 的结果。
     * @return 有效句柄或无效句柄
     */
    kale_device::ShaderHandle GetShader(const std::string& name) const;

    /**
     * 热重载：对已缓存的、path 匹配的所有着色器重新编译并更新缓存。
     * 需要 SetDevice 与 SetCompiler 已设置。
     * @param path 资源路径（与 LoadShader 时一致，用于匹配缓存键前缀）
     */
    void ReloadShader(const std::string& path);

    /** 启用/禁用热重载轮询（与 resource_management_layer 的 EnableHotReload 对齐） */
    void EnableHotReload(bool enable);
    /** 是否已启用热重载 */
    bool IsHotReloadEnabled() const { return hotReloadEnabled_; }

    /**
     * 每帧调用：检查已加载着色器路径的文件时间戳，若变化则调用 ReloadShader(path)
     * 并派发 RegisterReloadCallback 注册的回调（供应用层/材质层重新创建受影响的 Pipeline）。
     */
    void ProcessHotReload();

    /** 热重载成功后回调：(path)；用于重新创建受影响的 Pipeline */
    using ShaderReloadedCallback = std::function<void(const std::string& path)>;
    void RegisterReloadCallback(ShaderReloadedCallback cb);

    /** 构造与 LoadShader/GetShader 一致的缓存键，便于外部保存 name */
    static std::string MakeCacheKey(const std::string& path, kale_device::ShaderStage stage);

    /** 最后一次错误信息（来自 ShaderCompiler 或本类） */
    const std::string& GetLastError() const { return lastError_; }

private:
    kale::resource::ShaderCompiler* compiler_ = nullptr;
    kale_device::IRenderDevice* device_ = nullptr;
    std::unordered_map<std::string, kale_device::ShaderHandle> shaders_;
    std::string lastError_;

    bool hotReloadEnabled_ = false;
    std::unordered_map<std::string, std::filesystem::file_time_type> pathLastModified_;
    std::vector<ShaderReloadedCallback> reloadCallbacks_;
    mutable std::mutex hotReloadMutex_;

    void SetLastError(const std::string& msg) { lastError_ = msg; }
    void RecordPathLastModified(const std::string& path);
    static std::string StageSuffix(kale_device::ShaderStage stage);
    static kale_device::ShaderStage StageFromSuffix(const std::string& suffix);
};

}  // namespace kale::pipeline
