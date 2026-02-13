/**
 * @file material_loader.hpp
 * @brief 材质加载器：从 JSON 解析 PBR 材质，依赖加载纹理
 *
 * 与 resource_management_layer_design.md 5.7、phase8-8.2 对齐。
 * 格式：{ "albedo": "textures/brick.png", "metallic": 0.2, "roughness": 0.5, ... }
 */

#pragma once

#include <any>
#include <string>
#include <typeindex>

#include <kale_resource/resource_loader.hpp>
#include <kale_resource/resource_types.hpp>

namespace kale::pipeline {

class PBRMaterial;

/**
 * @brief 材质加载器：支持 .json，解析 PBR 键（albedo, normal, metallic, roughness 等），依赖 ResourceManager 加载纹理
 */
class MaterialLoader : public kale::resource::IResourceLoader {
public:
    bool Supports(const std::string& path) const override;
    std::any Load(const std::string& path, kale::resource::ResourceLoadContext& ctx) override;
    std::type_index GetResourceType() const override;

private:
    /** 从 JSON 文件解析并创建 PBRMaterial；失败返回 nullptr */
    kale::resource::Material* LoadJSON(const std::string& path, kale::resource::ResourceLoadContext& ctx);
};

}  // namespace kale::pipeline
