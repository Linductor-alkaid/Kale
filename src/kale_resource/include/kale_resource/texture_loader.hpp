/**
 * @file texture_loader.hpp
 * @brief 纹理加载器：PNG/JPG 等未压缩格式（stb_image）
 *
 * 与 resource_management_layer_design.md 5.6 对齐。
 * phase3-3.7：简单 TextureLoader，直接通过 RDI CreateTexture 传入数据（暂不通过 Staging）。
 */

#pragma once

#include <kale_resource/resource_loader.hpp>
#include <kale_resource/resource_types.hpp>

namespace kale::resource {

/**
 * @brief 纹理加载器：支持 .png、.jpg（使用 stb_image）
 *
 * Load 返回 std::unique_ptr<Texture>，通过 RDI CreateTexture 直接上传像素数据。
 */
class TextureLoader : public IResourceLoader {
public:
    bool Supports(const std::string& path) const override;
    std::any Load(const std::string& path, ResourceLoadContext& ctx) override;
    std::type_index GetResourceType() const override;

private:
    std::unique_ptr<Texture> LoadSTB(const std::string& path, ResourceLoadContext& ctx);
};

}  // namespace kale::resource
