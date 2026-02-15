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

bool SupportsDDS(const std::string& path) {
    return HasExtension(path, ".dds");
}

bool SupportsExtension(const std::string& path) {
    return HasExtension(path, ".png") || HasExtension(path, ".jpg") || HasExtension(path, ".jpeg")
           || SupportsKTX(path) || SupportsDDS(path);
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

/** 计算完整 mip 链层数：1 + floor(log2(min(w,h))) */
std::uint32_t MipLevelCount(std::uint32_t w, std::uint32_t h) {
    std::uint32_t m = (w < h) ? w : h;
    if (m == 0) return 1;
    std::uint32_t levels = 1;
    while (m > 1) {
        m /= 2;
        ++levels;
    }
    return levels;
}

/**
 * 生成未压缩格式的 mip 链（box 2x2 下采样）。
 * 返回 mipLevels 个 (width, height, data)；data 为连续像素，行优先。
 */
void GenerateMipChain(std::uint32_t w, std::uint32_t h, std::size_t bpp,
                      const void* baseData,
                      std::vector<std::pair<std::uint32_t, std::uint32_t>>& mipSizes,
                      std::vector<std::vector<std::uint8_t>>& mipData) {
    mipSizes.clear();
    mipData.clear();
    std::uint32_t cw = w, ch = h;
    const std::uint8_t* src = static_cast<const std::uint8_t*>(baseData);
    while (cw >= 1 && ch >= 1) {
        mipSizes.push_back({cw, ch});
        std::size_t size = static_cast<std::size_t>(cw) * ch * bpp;
        std::vector<std::uint8_t> level(size);
        if (cw == w && ch == h) {
            std::memcpy(level.data(), src, size);
        } else {
            const std::uint32_t prevW = mipSizes.size() >= 2
                ? mipSizes[static_cast<size_t>(mipSizes.size()) - 2].first
                : w;
            const std::uint32_t prevH = mipSizes.size() >= 2
                ? mipSizes[static_cast<size_t>(mipSizes.size()) - 2].second
                : h;
            const std::uint8_t* prev = mipData.back().data();
            for (std::uint32_t y = 0; y < ch; ++y) {
                for (std::uint32_t x = 0; x < cw; ++x) {
                    for (std::size_t c = 0; c < bpp; ++c) {
                        std::uint32_t px = x * 2, py = y * 2;
                        std::uint64_t sum = 0;
                        for (int dy = 0; dy < 2 && py + dy < prevH; ++dy)
                            for (int dx = 0; dx < 2 && px + dx < prevW; ++dx)
                                sum += prev[((py + dy) * prevW + (px + dx)) * bpp + c];
                        level[(y * cw + x) * bpp + c] = static_cast<std::uint8_t>(sum / 4);
                    }
                }
            }
        }
        mipData.push_back(std::move(level));
        if (cw == 1 && ch == 1) break;
        cw = std::max(1u, cw / 2);
        ch = std::max(1u, ch / 2);
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
    else if (SupportsDDS(path))
        tex = LoadDDS(path, ctx);
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

    std::uint32_t uw = static_cast<std::uint32_t>(w);
    std::uint32_t uh = static_cast<std::uint32_t>(h);
    std::uint32_t mipLevels = 1;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> mipSizes;
    std::vector<std::vector<std::uint8_t>> mipData;
    std::size_t bpp = BytesPerPixel(format);

    if (ctx.stagingMgr && ctx.device) {
        mipLevels = MipLevelCount(uw, uh);
        GenerateMipChain(uw, uh, bpp, uploadData, mipSizes, mipData);
    }

    kale_device::TextureDesc desc;
    desc.width = uw;
    desc.height = uh;
    desc.depth = 1;
    desc.mipLevels = mipLevels;
    desc.arrayLayers = 1;
    desc.format = format;
    desc.usage = kale_device::TextureUsage::Sampled
        | (ctx.stagingMgr ? kale_device::TextureUsage::Transfer : static_cast<kale_device::TextureUsage>(0));
    desc.isCube = false;

    kale_device::TextureHandle handle;

    if (ctx.stagingMgr && ctx.device) {
        /* Staging 路径：CreateTexture(desc, nullptr) + 逐 mip Allocate + SubmitUpload + FlushUploads + Free */
        handle = ctx.device->CreateTexture(desc, nullptr);
        if (!handle.IsValid()) {
            if (pixels) stbi_image_free(pixels);
            if (ctx.resourceManager) {
                ctx.resourceManager->SetLastError("CreateTexture failed for: " + path);
            }
            return nullptr;
        }
        std::vector<StagingAllocation> stagings;
        stagings.reserve(mipData.size());
        bool failed = false;
        for (size_t mip = 0; mip < mipData.size(); ++mip) {
            std::size_t uploadSize = mipData[mip].size();
            StagingAllocation staging = ctx.stagingMgr->Allocate(uploadSize);
            if (!staging.IsValid()) {
                if (ctx.resourceManager) {
                    ctx.resourceManager->SetLastError("Staging Allocate failed for mip " + std::to_string(mip) + ": " + path);
                }
                failed = true;
                break;
            }
            std::memcpy(staging.mappedPtr, mipData[mip].data(), uploadSize);
            std::uint32_t mw = mipSizes[mip].first;
            std::uint32_t mh = mipSizes[mip].second;
            ctx.stagingMgr->SubmitUpload(nullptr, staging, handle, static_cast<std::uint32_t>(mip), mw, mh, 1);
            stagings.push_back(staging);
        }
        if (failed) {
            for (const auto& s : stagings) ctx.stagingMgr->Free(s);
            ctx.device->DestroyTexture(handle);
            if (pixels) stbi_image_free(pixels);
            return nullptr;
        }
        kale_device::FenceHandle fence = ctx.stagingMgr->FlushUploads(ctx.device);
        if (fence.IsValid()) {
            ctx.device->WaitForFence(fence);
        }
        for (const auto& s : stagings) ctx.stagingMgr->Free(s);
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
    tex->width = uw;
    tex->height = uh;
    tex->format = format;
    tex->mipLevels = desc.mipLevels;
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

    const std::uint32_t numMips = numberOfMipmapLevels > 0 ? numberOfMipmapLevels : 1;
    /* 读取所有 mip：KTX1 每级为 imageSize (4 bytes) + data，无额外 padding */
    std::vector<std::vector<std::uint8_t>> mipLevelData(numMips);
    std::vector<std::uint32_t> mipWidths(numMips);
    std::vector<std::uint32_t> mipHeights(numMips);
    for (std::uint32_t mip = 0; mip < numMips; ++mip) {
        std::uint32_t imageSize = readU32();
        if (imageSize == 0) {
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX mip " + std::to_string(mip) + " size 0: " + path);
            return nullptr;
        }
        mipLevelData[mip].resize(imageSize);
        if (!f.read(reinterpret_cast<char*>(mipLevelData[mip].data()), imageSize)) {
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX mip " + std::to_string(mip) + " read failed: " + path);
            return nullptr;
        }
        mipWidths[mip] = std::max(1u, pixelWidth >> mip);
        mipHeights[mip] = std::max(1u, pixelHeight >> mip);
    }

    kale_device::TextureDesc desc;
    desc.width = pixelWidth;
    desc.height = pixelHeight;
    desc.depth = 1;
    desc.mipLevels = numMips;
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
        std::vector<StagingAllocation> stagings;
        stagings.reserve(numMips);
        bool failed = false;
        for (std::uint32_t mip = 0; mip < numMips; ++mip) {
            std::size_t sz = mipLevelData[mip].size();
            StagingAllocation staging = ctx.stagingMgr->Allocate(sz);
            if (!staging.IsValid()) {
                if (ctx.resourceManager) ctx.resourceManager->SetLastError("Staging Allocate failed for KTX mip " + std::to_string(mip) + ": " + path);
                failed = true;
                break;
            }
            std::memcpy(staging.mappedPtr, mipLevelData[mip].data(), sz);
            ctx.stagingMgr->SubmitUpload(nullptr, staging, handle, mip, mipWidths[mip], mipHeights[mip], 1);
            stagings.push_back(staging);
        }
        if (failed) {
            for (const auto& s : stagings) ctx.stagingMgr->Free(s);
            ctx.device->DestroyTexture(handle);
            return nullptr;
        }
        kale_device::FenceHandle fence = ctx.stagingMgr->FlushUploads(ctx.device);
        if (fence.IsValid()) ctx.device->WaitForFence(fence);
        for (const auto& s : stagings) ctx.stagingMgr->Free(s);
    } else {
        /* 无 Staging：仅 RGBA8 可走 CreateTexture(desc, data)（仅填 mip0）；压缩格式必须提供 StagingMgr */
        if (format != kale_device::Format::RGBA8_UNORM) {
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("KTX compressed format requires Staging: " + path);
            return nullptr;
        }
        handle = ctx.device->CreateTexture(desc, mipLevelData[0].data());
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

// DDS 格式常量（phase13-13.13）
namespace {
    constexpr std::uint32_t DDS_MAGIC = 0x20534444u;  /* "DDS " */
    constexpr std::uint32_t DDS_FOURCC_DXT1 = 0x31545844u;  /* 'DXT1' */
    constexpr std::uint32_t DDS_FOURCC_DXT5 = 0x35545844u;  /* 'DXT5' */
    constexpr std::uint32_t DDS_FOURCC_DX10 = 0x30315844u;  /* 'DX10' */
    constexpr std::uint32_t DDPF_FOURCC = 0x4u;
    /* DXGI_FORMAT */
    constexpr std::uint32_t DXGI_BC1_UNORM = 71u;
    constexpr std::uint32_t DXGI_BC2_UNORM = 74u;
    constexpr std::uint32_t DXGI_BC3_UNORM = 77u;
    constexpr std::uint32_t DXGI_BC5_UNORM = 79u;
    constexpr std::uint32_t DXGI_BC7_UNORM = 98u;

    kale_device::Format DdsFourCCToRdi(std::uint32_t fourCC) {
        switch (fourCC) {
            case DDS_FOURCC_DXT1: return kale_device::Format::BC1;
            case DDS_FOURCC_DXT5: return kale_device::Format::BC3;
            default: return kale_device::Format::Undefined;
        }
    }

    kale_device::Format DdsDxgiFormatToRdi(std::uint32_t dxgiFormat) {
        switch (dxgiFormat) {
            case DXGI_BC1_UNORM: return kale_device::Format::BC1;
            case DXGI_BC2_UNORM: /* 无 BC2 时跳过或当 BC3 用；rdi 仅有 BC1/BC3/BC5/BC7 */
                return kale_device::Format::Undefined;
            case DXGI_BC3_UNORM: return kale_device::Format::BC3;
            case DXGI_BC5_UNORM: return kale_device::Format::BC5;
            case DXGI_BC7_UNORM: return kale_device::Format::BC7;
            default: return kale_device::Format::Undefined;
        }
    }
}  // namespace

std::unique_ptr<Texture> TextureLoader::LoadDDS(const std::string& path, ResourceLoadContext& ctx) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("DDS open failed: " + path);
        return nullptr;
    }
    std::uint32_t magic = 0;
    if (!f.read(reinterpret_cast<char*>(&magic), 4) || magic != DDS_MAGIC) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("DDS invalid magic: " + path);
        return nullptr;
    }
    /* DDS_HEADER 124 字节：dwSize(4), dwFlags(4), dwHeight(4), dwWidth(4), dwPitchOrLinearSize(4), dwDepth(4), dwMipMapCount(4), reserved1[11](44), ddspf(32), dwCaps(4), ... */
    std::uint8_t header[124];
    if (!f.read(reinterpret_cast<char*>(header), 124)) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("DDS read header failed: " + path);
        return nullptr;
    }
    std::uint32_t dwHeight = *reinterpret_cast<std::uint32_t*>(header + 8);
    std::uint32_t dwWidth = *reinterpret_cast<std::uint32_t*>(header + 12);
    std::uint32_t dwMipMapCount = *reinterpret_cast<std::uint32_t*>(header + 24);
    std::uint32_t dwFourCC = *reinterpret_cast<std::uint32_t*>(header + 80);
    std::uint32_t dwFlagsPF = *reinterpret_cast<std::uint32_t*>(header + 76);

    if (dwWidth == 0 || dwHeight == 0) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("DDS zero size: " + path);
        return nullptr;
    }
    kale_device::Format format = kale_device::Format::Undefined;
    std::streamoff payloadOffset = 124;
    if ((dwFlagsPF & DDPF_FOURCC) && dwFourCC == DDS_FOURCC_DX10) {
        std::uint8_t dx10[20];
        if (!f.read(reinterpret_cast<char*>(dx10), 20)) {
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("DDS DX10 header read failed: " + path);
            return nullptr;
        }
        payloadOffset += 20;
        std::uint32_t dxgiFormat = *reinterpret_cast<std::uint32_t*>(dx10);
        format = DdsDxgiFormatToRdi(dxgiFormat);
    } else {
        format = DdsFourCCToRdi(dwFourCC);
    }
    if (format == kale_device::Format::Undefined) {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("DDS unsupported format: " + path);
        return nullptr;
    }

    const std::uint32_t numMips = (dwMipMapCount > 0) ? dwMipMapCount : 1;
    std::vector<std::vector<std::uint8_t>> mipLevelData(numMips);
    std::vector<std::uint32_t> mipWidths(numMips);
    std::vector<std::uint32_t> mipHeights(numMips);
    std::uint32_t w = dwWidth, h = dwHeight;
    for (std::uint32_t mip = 0; mip < numMips; ++mip) {
        std::size_t sz = CompressedMipSize(format, w, h);
        if (sz == 0) {
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("DDS mip size 0: " + path);
            return nullptr;
        }
        mipLevelData[mip].resize(sz);
        if (!f.read(reinterpret_cast<char*>(mipLevelData[mip].data()), static_cast<std::streamsize>(sz))) {
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("DDS mip read failed: " + path);
            return nullptr;
        }
        mipWidths[mip] = w;
        mipHeights[mip] = h;
        if (w > 1 || h > 1) {
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
    }

    kale_device::TextureDesc desc;
    desc.width = dwWidth;
    desc.height = dwHeight;
    desc.depth = 1;
    desc.mipLevels = numMips;
    desc.arrayLayers = 1;
    desc.format = format;
    desc.usage = kale_device::TextureUsage::Sampled | kale_device::TextureUsage::Transfer;
    desc.isCube = false;

    kale_device::TextureHandle handle;
    if (ctx.stagingMgr && ctx.device) {
        handle = ctx.device->CreateTexture(desc, nullptr);
        if (!handle.IsValid()) {
            if (ctx.resourceManager) ctx.resourceManager->SetLastError("CreateTexture failed for DDS: " + path);
            return nullptr;
        }
        std::vector<StagingAllocation> stagings;
        stagings.reserve(numMips);
        bool failed = false;
        for (std::uint32_t mip = 0; mip < numMips; ++mip) {
            std::size_t sz = mipLevelData[mip].size();
            StagingAllocation staging = ctx.stagingMgr->Allocate(sz);
            if (!staging.IsValid()) {
                if (ctx.resourceManager) ctx.resourceManager->SetLastError("Staging Allocate failed for DDS mip " + std::to_string(mip) + ": " + path);
                failed = true;
                break;
            }
            std::memcpy(staging.mappedPtr, mipLevelData[mip].data(), sz);
            ctx.stagingMgr->SubmitUpload(nullptr, staging, handle, mip, mipWidths[mip], mipHeights[mip], 1);
            stagings.push_back(staging);
        }
        if (failed) {
            for (const auto& s : stagings) ctx.stagingMgr->Free(s);
            ctx.device->DestroyTexture(handle);
            return nullptr;
        }
        kale_device::FenceHandle fence = ctx.stagingMgr->FlushUploads(ctx.device);
        if (fence.IsValid()) ctx.device->WaitForFence(fence);
        for (const auto& s : stagings) ctx.stagingMgr->Free(s);
    } else {
        if (ctx.resourceManager) ctx.resourceManager->SetLastError("DDS compressed format requires Staging: " + path);
        return nullptr;
    }

    auto tex = std::make_unique<Texture>();
    tex->handle = handle;
    tex->width = dwWidth;
    tex->height = dwHeight;
    tex->format = format;
    tex->mipLevels = numMips;
    return tex;
}

}  // namespace kale::resource
