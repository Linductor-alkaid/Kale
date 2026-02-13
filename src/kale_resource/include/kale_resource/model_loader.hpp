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
 * @brief 模型加载器：支持 .gltf、.glb（tinygltf）、.obj（简易解析）
 *
 * Load 返回 std::unique_ptr<Mesh>，通过 RDI CreateBuffer 创建顶点/索引缓冲。
 * LOD：路径约定 path#lodN 仅加载 glTF 第 N 个 mesh（0-based）；无 #lod 时合并全部 mesh。
 */
class ModelLoader : public IResourceLoader {
public:
    bool Supports(const std::string& path) const override;
    std::any Load(const std::string& path, ResourceLoadContext& ctx) override;
    std::type_index GetResourceType() const override;

private:
    /** @param lodIndex 若 >=0 仅加载该 mesh 作为单 LOD；<0 合并全部 mesh */
    std::unique_ptr<Mesh> LoadGLTF(const std::string& path, ResourceLoadContext& ctx, int lodIndex = -1);
    std::unique_ptr<Mesh> LoadOBJ(const std::string& path, ResourceLoadContext& ctx);
};

}  // namespace kale::resource
