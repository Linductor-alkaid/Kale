/**
 * @file texture_loader.cpp
 * @brief TextureLoader 实现：stb_image 加载 PNG/JPG；优先通过 Staging 上传（phase6-6.4）
 */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include <kale_device/rdi_types.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_resource/staging_memory_manager.hpp>
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

bool SupportsKTX(const std::string& path) {
    return HasExtension(path, ".ktx");
}

bool SupportsExtension(const std::string& path) {
    return HasExtension(path, ".png") || HasExtension(path, ".jpg") || HasExtension(path, ".jpeg")
           || SupportsKTX(path);
}

/** 按 Format 返回每像素字节数（仅支持 R8/RG8/RGBA8） */
std::size_t BytesPerPixel(kale_device::Format format) {
    switch (format) {
        case kale_device::Format::R8_UNORM: return 1;
        case kale_device::Format::RG8_UNORM: return 2;
        case kale_device::Format::RGBA8_UNORM:
        case kale_device::Format::RGBA8_SRGB: return 4;
        default: return 4;
    }
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
    std::unique_ptr<Texture> tex;
    if (SupportsKTX(path))
        tex = LoadKTX(path, ctx);
    else
        tex = LoadSTB(path, ctx);
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

    kale_device::TextureHandle handle;

    if (ctx.stagingMgr && ctx.device) {
        /* Staging 路径：CreateTexture(desc, nullptr) + Allocate + SubmitUpload + FlushUploads + Free(alloc, fence) */
        handle = ctx.device->CreateTexture(desc, nullptr);
        if (!handle.IsValid()) {
            if (pixels) stbi_image_free(pixels);
            if (ctx.resourceManager) {
                ctx.resourceManager->SetLastError("CreateTexture failed for: " + path);
            }
            return nullptr;
        }
        std::size_t bpp = BytesPerPixel(format);
        std::size_t uploadSize = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * desc.depth * bpp;
        StagingAllocation staging = ctx.stagingMgr->Allocate(uploadSize);
        if (!staging.IsValid()) {
            ctx.device->DestroyTexture(handle);
            if (pixels) stbi_image_free(pixels);
            if (ctx.resourceManager) {
                ctx.resourceManager->SetLastError("Staging Allocate failed for: " + path);
            }
            return nullptr;
        }
        std::memcpy(staging.mappedPtr, uploadData, uploadSize);
        ctx.stagingMgr->SubmitUpload(nullptr, staging, handle, 0,
                                    static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), 1);
        kale_device::FenceHandle fence = ctx.stagingMgr->FlushUploads(ctx.device);
        if (fence.IsValid()) {
            ctx.device->WaitForFence(fence);
        }
        /* 上传已完成，直接回收到池（无需延迟 Free(alloc, fence)） */
        ctx.stagingMgr->Free(staging);
        if (pixels) stbi_image_free(pixels);
    } else {
        /* 无 Staging 时回退：直接 CreateTexture(desc, data) */
        handle = ctx.device->CreateTexture(desc, uploadData);
        if (pixels) stbi_image_free(pixels);
        if (!handle.IsValid()) {
            if (ctx.resourceManager) {
                ctx.resourceManager->SetLastError("CreateTexture failed for: " + path);
            }
            return nullptr;
        }
    }

    auto tex = std::make_unique<Texture>();
    tex->handle = handle;
    tex->width = static_cast<std::uint32_t>(w);
    tex->height = static_cast<std::uint32_t>(h);
    tex->format = format;
    tex->mipLevels = 1;
    return tex;
}

// KTX1 格式常量（OpenGL internal format，不依赖 GL 头文件）
namespace {
    constexpr std::uint32_t KTX1_GL_RGBA8 = 0x8058u;
    constexpr std::uint32_t KTX1_GL_RGB8  = 0x8051u;
    constexpr std::uint32_t KTX1_GL_COMPRESSED_RGBA_S3TC_DXT1 = 0x83F1u;
    constexpr std::uint32_t KTX1_GL_COMPRESSED_RGBA_S3TC_DXT5 = 0x83F3u;
    constexpr std::uint32_t KTX1_GL_COMPRESSED_RG_RGTC2       = 0x8DBDu;
    constexpr std::uint32_t KTX1_GL_COMPRESSED_RGBA_BPTC_UNORM = 0x8E8Cu;
    constexpr std::uint8_t KTX1_IDENTIFIER[12] = {
        0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A
    };

    kale_device::Format GlInternalFormatToRdi(std::uint32_t glInternalFormat) {
        switch (glInternalFormat) {
            case KTX1_GL_RGBA8:
            case KTX1_GL_RGB8:  /* 按 RGBA8 加载，RGB 可后续扩展 */
                return kale_device::Format::RGBA8_UNORM;
            case KTX1_GL_COMPRESSED_RGBA_S3TC_DXT1:
                return kale_device::Format::BC1;
            case KTX1_GL_COMPRESSED_RGBA_S3TC_DXT5:
                return kale_device::Format::BC3;
            case KTX1_GL_COMPRESSED_RG_RGTC2:
                return kale_device::Format::BC5;
            case KTX1_GL_COMPRESSED_RGBA_BPTC_UNORM:
                return kale_device::Format::BC7;
            default:
                return kale_device::Format::Undefined;
        }
    }

    std::size_t CompressedMipSize(kale_device::Format format, std::uint32_t w, std::uint32_t h) {
        std::uint32_t bw = (w + 3u) / 4u;
        std::uint32_t bh = (h + 3u) / 4u;
        switch (format) {
            case kale_device::Format::BC1:
                return static_cast<std::size_t>(bw) * bh * 8u;
            case kale_device::Format::BC3:
            case kale_device::Format::BC5:
            case kale_device::Format::BC7:
                return static_cast<std::size_t>(bw) * bh * 16u;
            default:
                return 0;
        }
    }
}  // namespace

std::unique_ptr<Texture> TextureLoader::LoadKTX(const std::string& path, ResourceLoadContext& ctx) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX open failed: " + path);
        return nullptr;
    }
    std::uint8_t id[12];
    if (!f.read(reinterpret_cast<char*>(id), 12) || std::memcmp(id, KTX1_IDENTIFIER, 12) != 0) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX invalid identifier: " + path);
        return nullptr;
    }
    std::uint32_t endianness = 0;
    if (!f.read(reinterpret_cast<char*>(&endianness), 4)) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX read header failed: " + path);
        return nullptr;
    }
    bool swapEndian = (endianness == 0x04030201u);
    auto readU32 = [&f, swapEndian]() -> std::uint32_t {
        std::uint32_t v = 0;
        if (!f.read(reinterpret_cast<char*>(&v), 4)) return 0;
        if (swapEndian) {
            v = ((v >> 24) & 0xFFu) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | ((v << 24) & 0xFF000000u);
        }
        return v;
    };
    std::uint32_t glType = readU32();
    std::uint32_t glFormat = readU32();
    std::uint32_t glInternalFormat = readU32();
    (void)readU32();  /* glBaseInternalFormat */
    std::uint32_t pixelWidth = readU32();
    std::uint32_t pixelHeight = readU32();
    std::uint32_t pixelDepth = readU32();
    (void)readU32();  /* numberOfArrayElements */
    std::uint32_t numberOfFaces = readU32();
    std::uint32_t numberOfMipmapLevels = readU32();
    std::uint32_t bytesOfKeyValueData = readU32();

    if (pixelWidth == 0 || pixelHeight == 0) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX zero size: " + path);
        return nullptr;
    }
    if (numberOfFaces != 1u) {
        /* 暂不支持 cube */
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX cube not supported: " + path);
        return nullptr;
    }
    kale_device::Format format = GlInternalFormatToRdi(glInternalFormat);
    if (format == kale_device::Format::Undefined) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX unsupported format: " + path);
        return nullptr;
    }

    /* 跳过 key/value */
    if (bytesOfKeyValueData > 0) {
        f.seekg(static_cast<std::streamoff>(bytesOfKeyValueData), std::ios::cur);
        if (!f.good()) {
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX key/value skip failed: " + path);
            return nullptr;
        }
    }

    /* 读 mip0：imageSize (4 bytes) + data */
    std::uint32_t imageSize = readU32();
    if (imageSize == 0) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX mip0 size 0: " + path);
        return nullptr;
    }
    std::vector<std::uint8_t> mip0Data(imageSize);
    if (!f.read(reinterpret_cast<char*>(mip0Data.data()), imageSize)) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX mip0 read failed: " + path);
        return nullptr;
    }

    kale_device::TextureDesc desc;
    desc.width = pixelWidth;
    desc.height = pixelHeight;
    desc.depth = 1;
    desc.mipLevels = numberOfMipmapLevels > 0 ? numberOfMipmapLevels : 1;
    desc.arrayLayers = 1;
    desc.format = format;
    desc.usage = kale_device::TextureUsage::Sampled | kale_device::TextureUsage::Transfer;
    desc.isCube = false;

    kale_device::TextureHandle handle;
    if (ctx.stagingMgr && ctx.device) {
        handle = ctx.device->CreateTexture(desc, nullptr);
        if (!handle.IsValid()) {
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("CreateTexture failed for KTX: " + path);
            return nullptr;
        }
        StagingAllocation staging = ctx.stagingMgr->Allocate(imageSize);
        if (!staging.IsValid()) {
            ctx.device->DestroyTexture(handle);
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("Staging Allocate failed for KTX: " + path);
            return nullptr;
        }
        std::memcpy(staging.mappedPtr, mip0Data.data(), imageSize);
        ctx.stagingMgr->SubmitUpload(nullptr, staging, handle, 0, pixelWidth, pixelHeight, 1);
        kale_device::FenceHandle fence = ctx.stagingMgr->FlushUploads(ctx.device);
        if (fence.IsValid()) ctx.device->WaitForFence(fence);
        ctx.stagingMgr->Free(staging);
    } else {
        /* 无 Staging：仅 RGBA8 可走 CreateTexture(desc, data)；压缩格式必须提供 StagingMgr */
        if (format != kale_device::Format::RGBA8_UNORM) {
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX compressed format requires Staging: " + path);
            return nullptr;
        }
        handle = ctx.device->CreateTexture(desc, mip0Data.data());
        if (!handle.IsValid()) {
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("CreateTexture failed for KTX: " + path);
            return nullptr;
        }
    }

    auto tex = std::make_unique<Texture>();
    tex->handle = handle;
    tex->width = pixelWidth;
    tex->height = pixelHeight;
    tex->format = format;
    tex->mipLevels = desc.mipLevels;
    return tex;
}

}  // namespace kale::resource
