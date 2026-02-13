/**
 * @file material_pipeline_reload_registry.hpp
 * @brief 材质/着色器热重载时重新创建 Pipeline 的注册表
 *
 * phase13-13.15：当 ShaderManager 触发 ReloadCallback(path) 时，对使用该 path 的材质
 * 重新从 ShaderManager 取新 ShaderHandle、CreatePipeline、Destroy 旧 Pipeline、SetPipeline。
 * 与 ShaderManager::RegisterReloadCallback 配合使用。
 */

#pragma once

#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

#include <string>
#include <vector>

namespace kale::pipeline {

class Material;
class ShaderManager;

/**
 * 注册表：维护 (Material*, vertexPath, fragmentPath, PipelineDesc 除 shaders 外)，
 * 当 OnShaderReloaded(path) 时对匹配的材质重新创建 Pipeline。
 */
class MaterialPipelineReloadRegistry {
public:
    MaterialPipelineReloadRegistry() = default;

    void SetShaderManager(ShaderManager* mgr) { shaderManager_ = mgr; }
    void SetDevice(kale_device::IRenderDevice* device) { device_ = device; }
    ShaderManager* GetShaderManager() const { return shaderManager_; }
    kale_device::IRenderDevice* GetDevice() const { return device_; }

    /**
     * 注册材质：当其 vertexPath 或 fragmentPath 对应着色器热重载时，将用新 shader 重新创建 Pipeline 并 SetPipeline。
     * @param mat 材质（非占有）
     * @param vertexPath 顶点着色器路径（与 ShaderManager::LoadShader 一致）
     * @param fragmentPath 片段着色器路径
     * @param descWithoutShaders Pipeline 描述（shaders 留空，将在重载时填充）
     */
    void RegisterMaterial(Material* mat,
                          const std::string& vertexPath,
                          const std::string& fragmentPath,
                          const kale_device::PipelineDesc& descWithoutShaders);

    /** 取消注册 */
    void UnregisterMaterial(Material* mat);

    /**
     * 由 ShaderManager::RegisterReloadCallback 调用：path 对应着色器已重载，对使用该 path 的材质重新创建 Pipeline。
     */
    void OnShaderReloaded(const std::string& path);

private:
    struct Entry {
        Material* mat = nullptr;
        std::string vertexPath;
        std::string fragmentPath;
        kale_device::PipelineDesc descWithoutShaders;
    };
    ShaderManager* shaderManager_ = nullptr;
    kale_device::IRenderDevice* device_ = nullptr;
    std::vector<Entry> entries_;
};

}  // namespace kale::pipeline
