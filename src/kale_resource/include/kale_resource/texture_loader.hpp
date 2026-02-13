/**
 * @file texture_loader.hpp
 * @brief 纹理加载器：PNG/JPG 等未压缩格式（stb_image）
 *
 * 与 resource_management_layer_design.md 5.6 对齐。
 * phase3-3.7：简单 TextureLoader。
 * phase6-6.4：当 ctx.stagingMgr 非空时通过 Staging 池上传；否则回退为 CreateTexture(desc, data)。
 */

#pragma once

#include <kale_resource/resource_loader.hpp>
#include <kale_resource/resource_types.hpp>

namespace kale::resource {

/**
 * @brief 纹理加载器：支持 .png、.jpg（stb_image）、.ktx（KTX1 压缩/未压缩）
 *
 * Load 返回 std::unique_ptr<Texture>；Staging 可用时经 Staging 上传，否则回退 CreateTexture(desc, data)。
 * phase12-12.5：LoadKTX 支持 KTX1，根据 glInternalFormat 选择 RDI Format（RGBA8、BC1/BC3/BC5/BC7）。
 */
class TextureLoader : public IResourceLoader {
public:
    bool Supports(const std::string& path) const override;
    std::any Load(const std::string& path, ResourceLoadContext& ctx) override;
    std::type_index GetResourceType() const override;

private:
    std::unique_ptr<Texture> LoadSTB(const std::string& path, ResourceLoadContext& ctx);
    std::unique_ptr<Texture> LoadKTX(const std::string& path, ResourceLoadContext& ctx);
};

}  // namespace kale::resource
