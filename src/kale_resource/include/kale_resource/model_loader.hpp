/**
 * @file model_loader.hpp
 * @brief 模型加载器：glTF 顶点/索引解析（tinygltf）
 *
 * 与 resource_management_layer_design.md 5.5 对齐。
 * phase3-3.8：简单 ModelLoader，支持 .gltf/.glb，生成 Mesh（vertexBuffer、indexBuffer、bounds、subMeshes）。
 * phase6-6.5：当 ctx.stagingMgr 非空时通过 Staging 上传顶点/索引；否则回退为 CreateBuffer(desc, data）。
 */

#pragma once

#include <kale_resource/resource_loader.hpp>
#include <kale_resource/resource_types.hpp>

namespace kale::resource {

/**
 * @brief 模型加载器：支持 .gltf、.glb（使用 tinygltf）
 *
 * Load 返回 std::unique_ptr<Mesh>，通过 RDI CreateBuffer 创建顶点/索引缓冲。
 */
class ModelLoader : public IResourceLoader {
public:
    bool Supports(const std::string& path) const override;
    std::any Load(const std::string& path, ResourceLoadContext& ctx) override;
    std::type_index GetResourceType() const override;

private:
    std::unique_ptr<Mesh> LoadGLTF(const std::string& path, ResourceLoadContext& ctx);
};

}  // namespace kale::resource
