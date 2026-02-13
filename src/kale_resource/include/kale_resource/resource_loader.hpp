/**
 * @file resource_loader.hpp
 * @brief 资源加载器接口 IResourceLoader 与加载上下文 ResourceLoadContext
 *
 * 与 resource_management_layer_design.md 5.4 对齐。
 * Loader 实现（TextureLoader、ModelLoader、MaterialLoader）据此接口实现。
 */

#pragma once

#include <any>
#include <string>
#include <typeindex>

#include <kale_device/render_device.hpp>

namespace kale::resource {

class StagingMemoryManager;
class ResourceManager;

/**
 * @brief 资源加载上下文：供 IResourceLoader::Load 使用
 *
 * device：创建 GPU 资源（Buffer、Texture 等）
 * stagingMgr：暂存缓冲与上传（phase6 实现后可非空）
 * resourceManager：加载依赖资源（如 Material 依赖 Texture）
 * shaderManager：可选，kale::pipeline::ShaderManager*，供 MaterialLoader 热重载时创建 Pipeline（phase13-13.15）
 */
struct ResourceLoadContext {
    kale_device::IRenderDevice* device = nullptr;
    StagingMemoryManager* stagingMgr = nullptr;
    ResourceManager* resourceManager = nullptr;
    void* shaderManager = nullptr;  // kale::pipeline::ShaderManager* when used by MaterialLoader
};

/**
 * @brief 资源加载器抽象接口
 *
 * 各具体加载器（TextureLoader、ModelLoader、MaterialLoader）实现此接口，
 * 通过 Supports(path) 声明支持的路径/扩展名，通过 Load 执行同步加载。
 */
class IResourceLoader {
public:
    virtual ~IResourceLoader() = default;

    /**
     * @brief 是否支持该路径（通常按扩展名判断）
     */
    virtual bool Supports(const std::string& path) const = 0;

    /**
     * @brief 同步加载资源
     * @param path 资源路径（可为 ResolvePath 后的完整路径）
     * @param ctx 加载上下文（device、stagingMgr、resourceManager）
     * @return 加载成功返回持有资源的 std::any（如 std::unique_ptr<Texture>），失败返回空 std::any
     */
    virtual std::any Load(const std::string& path, ResourceLoadContext& ctx) = 0;

    /**
     * @brief 返回本 Loader 加载的资源类型（如 typeid(Mesh)、typeid(Texture)）
     */
    virtual std::type_index GetResourceType() const = 0;
};

}  // namespace kale::resource
