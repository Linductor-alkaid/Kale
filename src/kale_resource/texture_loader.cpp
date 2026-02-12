/**
 * @file texture_loader.cpp
 * @brief TextureLoader 实现：stb_image 加载 PNG/JPG，RDI CreateTexture 直接上传
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include <kale_device/rdi_types.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_resource/texture_loader.hpp>

namespace kale::resource {

namespace {

bool HasExtension(const std::string& path, const char* ext) {
    size_t plen = path.size();
    size_t elen = std::strlen(ext);
    if (plen < elen) return false;
    std::string suffix = path.substr(plen - elen);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return suffix == ext;
}

bool SupportsExtension(const std::string& path) {
    return HasExtension(path, ".png") || HasExtension(path, ".jpg") || HasExtension(path, ".jpeg");
}

}  // namespace

bool TextureLoader::Supports(const std::string& path) const {
    return SupportsExtension(path);
}

std::type_index TextureLoader::GetResourceType() const {
    return typeid(Texture);
}

std::any TextureLoader::Load(const std::string& path, ResourceLoadContext& ctx) {
    if (!ctx.device) return {};
    auto tex = LoadSTB(path, ctx);
    if (!tex) return {};
    return std::any(tex.release());
}

std::unique_ptr<Texture> TextureLoader::LoadSTB(const std::string& path, ResourceLoadContext& ctx) {
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 0);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        if (ctx.resourceManager) {
            ctx.resourceManager->SetLastError("stb_image failed to load: " + path);
        }
        return nullptr;
    }

    kale_device::Format format = kale_device::Format::Undefined;
    const void* uploadData = pixels;
    std::vector<std::uint8_t> rgbaExpand;
    if (channels == 1) {
        format = kale_device::Format::R8_UNORM;
    } else if (channels == 2) {
        format = kale_device::Format::RG8_UNORM;
    } else if (channels == 3) {
        format = kale_device::Format::RGBA8_UNORM;
        rgbaExpand.resize(static_cast<size_t>(w) * h * 4);
        for (int i = 0; i < w * h; ++i) {
            rgbaExpand[static_cast<size_t>(i) * 4 + 0] = pixels[i * 3 + 0];
            rgbaExpand[static_cast<size_t>(i) * 4 + 1] = pixels[i * 3 + 1];
            rgbaExpand[static_cast<size_t>(i) * 4 + 2] = pixels[i * 3 + 2];
            rgbaExpand[static_cast<size_t>(i) * 4 + 3] = 255;
        }
        uploadData = rgbaExpand.data();
        stbi_image_free(pixels);
        pixels = nullptr;
    } else {
        format = kale_device::Format::RGBA8_UNORM;
    }

    kale_device::TextureDesc desc;
    desc.width = static_cast<std::uint32_t>(w);
    desc.height = static_cast<std::uint32_t>(h);
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.format = format;
    desc.usage = kale_device::TextureUsage::Sampled;
    desc.isCube = false;

    kale_device::TextureHandle handle = ctx.device->CreateTexture(desc, uploadData);
    if (pixels) stbi_image_free(pixels);

    if (!handle.IsValid()) {
        if (ctx.resourceManager) {
            ctx.resourceManager->SetLastError("CreateTexture failed for: " + path);
        }
        return nullptr;
    }

    auto tex = std::make_unique<Texture>();
    tex->handle = handle;
    tex->width = static_cast<std::uint32_t>(w);
    tex->height = static_cast<std::uint32_t>(h);
    tex->format = format;
    tex->mipLevels = 1;
    return tex;
}

}  // namespace kale::resource
