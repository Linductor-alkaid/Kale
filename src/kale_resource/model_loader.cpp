/**
 * @file model_loader.cpp
 * @brief ModelLoader 实现：tinygltf 解析 glTF，生成 Mesh（顶点/索引缓冲、bounds、subMeshes）
 */

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <typeindex>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
// 与根 CMake 对 tinygltf 的 TINYGLTF_NO_STB_IMAGE 一致，避免头文件内默认取址 &tinygltf::LoadImageData 导致未定义引用
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>
// 使用 kale_resource 内 texture_loader 提供的 stb 实现，不在此定义 STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <kale_device/rdi_types.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_resource/staging_memory_manager.hpp>
#include <kale_resource/model_loader.hpp>

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

// 去掉 #lodN 后缀得到用于扩展检查的路径
std::string PathWithoutLodSuffix(const std::string& path) {
    std::string::size_type hash = path.find('#');
    if (hash == std::string::npos) return path;
    return path.substr(0, hash);
}

// 解析 path#lodN，返回 (basePath, lodIndex)；无 #lod 时 lodIndex = -1
void ParseLodPath(const std::string& path, std::string* outBasePath, int* outLodIndex) {
    std::string::size_type hash = path.find('#');
    if (hash == std::string::npos) {
        *outBasePath = path;
        *outLodIndex = -1;
        return;
    }
    *outBasePath = path.substr(0, hash);
    std::string suffix = path.substr(hash + 1);
    if (suffix.size() >= 4 && suffix.substr(0, 3) == "lod") {
        try {
            *outLodIndex = std::stoi(suffix.substr(3));
            if (*outLodIndex < 0) *outLodIndex = -1;
        } catch (...) {
            *outLodIndex = -1;
        }
    } else {
        *outLodIndex = -1;
    }
}

bool SupportsExtension(const std::string& path) {
    std::string base = PathWithoutLodSuffix(path);
    return HasExtension(base, ".gltf") || HasExtension(base, ".glb") || HasExtension(base, ".obj");
}

// 从文件路径得到所在目录（不含末尾 /）；若无目录则返回 "." 
std::string GetBaseDir(const std::string& path) {
    std::string::size_type pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    return path.substr(0, pos);
}

// 顶点格式：位置(3) + 法线(3) + UV(2) = 8 float
struct VertexPNT {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// 从 tinygltf 的 accessor 读取原始指针与步长（按分量）
const unsigned char* GetAccessorData(const tinygltf::Model& model, int accessorIndex,
                                      size_t* outStrideBytes, size_t* outCount) {
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size())
        return nullptr;
    const tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(accessorIndex)];
    if (acc.bufferView < 0 || static_cast<size_t>(acc.bufferView) >= model.bufferViews.size())
        return nullptr;
    const tinygltf::BufferView& bv = model.bufferViews[static_cast<size_t>(acc.bufferView)];
    if (bv.buffer < 0 || static_cast<size_t>(bv.buffer) >= model.buffers.size())
        return nullptr;
    const tinygltf::Buffer& buf = model.buffers[static_cast<size_t>(bv.buffer)];
    if (buf.data.empty()) return nullptr;

    size_t componentSize = static_cast<size_t>(tinygltf::GetComponentSizeInBytes(acc.componentType));
    int numComp = tinygltf::GetNumComponentsInType(acc.type);
    if (componentSize == 0 || numComp <= 0) return nullptr;

    size_t stride = (bv.byteStride > 0) ? static_cast<size_t>(bv.byteStride)
                                        : (componentSize * static_cast<size_t>(numComp));
    size_t start = bv.byteOffset + acc.byteOffset;
    if (start + acc.count * stride > buf.data.size()) return nullptr;

    *outStrideBytes = stride;
    *outCount = acc.count;
    return buf.data.data() + start;
}

// 从 accessor 读 vec3 float 到数组（按顶点索引）
void ReadVec3Float(const tinygltf::Model& model, int accessorIndex, glm::vec3* out, size_t maxCount) {
    size_t strideBytes, count;
    const unsigned char* src = GetAccessorData(model, accessorIndex, &strideBytes, &count);
    if (!src || count > maxCount) return;
    for (size_t i = 0; i < count; ++i) {
        const float* f = reinterpret_cast<const float*>(src + i * strideBytes);
        out[i].x = f[0];
        out[i].y = f[1];
        out[i].z = f[2];
    }
}

void ReadVec2Float(const tinygltf::Model& model, int accessorIndex, glm::vec2* out, size_t maxCount) {
    size_t strideBytes, count;
    const unsigned char* src = GetAccessorData(model, accessorIndex, &strideBytes, &count);
    if (!src || count > maxCount) return;
    for (size_t i = 0; i < count; ++i) {
        const float* f = reinterpret_cast<const float*>(src + i * strideBytes);
        out[i].x = f[0];
        out[i].y = f[1];
    }
}

// 读取索引（支持 uint16 / uint32）
void ReadIndices(const tinygltf::Model& model, int accessorIndex, std::vector<std::uint32_t>* out) {
    if (accessorIndex < 0 || static_cast<size_t>(accessorIndex) >= model.accessors.size()) return;
    const tinygltf::Accessor& acc = model.accessors[static_cast<size_t>(accessorIndex)];
    size_t strideBytes, count;
    const unsigned char* src = GetAccessorData(model, accessorIndex, &strideBytes, &count);
    if (!src) return;
    out->resize(count);
    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        for (size_t i = 0; i < count; ++i)
            (*out)[i] = reinterpret_cast<const std::uint16_t*>(src + i * strideBytes)[0];
    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        for (size_t i = 0; i < count; ++i)
            (*out)[i] = reinterpret_cast<const std::uint32_t*>(src + i * strideBytes)[0];
    }
}

// tinygltf 在 TINYGLTF_NO_STB_IMAGE 下无默认 image loader，用 stb 解码（实现来自 texture_loader.cpp）
bool LoadImageDataStb(tinygltf::Image* image, const int image_idx, std::string* err,
                      std::string* warn, int req_width, int req_height,
                      const unsigned char* bytes, int size, void* user_data) {
    (void)image_idx;
    (void)warn;
    (void)req_width;
    (void)req_height;
    (void)user_data;
    if (!bytes || size <= 0) {
        if (err) *err += "LoadImageDataStb: no data\n";
        return false;
    }
    int w = 0, h = 0, comp = 0;
    const int req_comp = 4;
    unsigned char* data = stbi_load_from_memory(bytes, size, &w, &h, &comp, req_comp);
    if (!data) {
        if (err) *err += "LoadImageDataStb: stbi_load_from_memory failed\n";
        return false;
    }
    image->width = w;
    image->height = h;
    image->component = req_comp;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    const size_t total = static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(req_comp);
    image->image.resize(total);
    std::memcpy(image->image.data(), data, total);
    stbi_image_free(data);
    return true;
}

// 简易 OBJ 解析：v, vn, vt, f（三角形）；未提供时法线/UV 用默认值
bool ParseOBJ(const std::string& path,
              std::vector<VertexPNT>* outVertices,
              std::vector<std::uint32_t>* outIndices,
              std::string* outError) {
    std::ifstream f(path);
    if (!f.is_open()) {
        if (outError) *outError = "cannot open file: " + path;
        return false;
    }
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;
    outVertices->clear();
    outIndices->clear();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string tok;
        iss >> tok;
        if (tok == "v") {
            float x, y, z;
            if (iss >> x >> y >> z)
                positions.push_back({x, y, z});
        } else if (tok == "vn") {
            float x, y, z;
            if (iss >> x >> y >> z)
                normals.push_back({x, y, z});
        } else if (tok == "vt") {
            float u, v;
            if (iss >> u >> v)
                uvs.push_back({u, v});
        } else if (tok == "f") {
            // 支持 f v1 v2 v3 或 f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3
            std::uint32_t idx[3];
            for (int i = 0; i < 3; ++i) {
                std::string v;
                if (!(iss >> v)) {
                    if (outError) *outError = "OBJ f needs 3 vertices: " + line;
                    return false;
                }
                std::uint32_t vi = 0, vti = 0, vni = 0;
                size_t slash1 = v.find('/');
                if (slash1 == std::string::npos) {
                    vi = static_cast<std::uint32_t>(std::stoul(v));
                } else {
                    vi = static_cast<std::uint32_t>(std::stoul(v.substr(0, slash1)));
                    size_t slash2 = v.find('/', slash1 + 1);
                    if (slash2 != std::string::npos && slash2 > slash1 + 1)
                        vti = static_cast<std::uint32_t>(std::stoul(v.substr(slash1 + 1, slash2 - slash1 - 1)));
                    if (slash2 != std::string::npos && slash2 + 1 < v.size())
                        vni = static_cast<std::uint32_t>(std::stoul(v.substr(slash2 + 1)));
                }
                if (vi > 0 && vi <= positions.size()) {
                    VertexPNT vert;
                    const glm::vec3& p = positions[vi - 1];
                    vert.px = p.x; vert.py = p.y; vert.pz = p.z;
                    if (vni > 0 && vni <= normals.size()) {
                        const glm::vec3& n = normals[vni - 1];
                        vert.nx = n.x; vert.ny = n.y; vert.nz = n.z;
                    } else {
                        vert.nx = 0.f; vert.ny = 0.f; vert.nz = 1.f;
                    }
                    if (vti > 0 && vti <= uvs.size()) {
                        const glm::vec2& uv = uvs[vti - 1];
                        vert.u = uv.x; vert.v = uv.y;
                    } else {
                        vert.u = 0.f; vert.v = 0.f;
                    }
                    idx[i] = static_cast<std::uint32_t>(outVertices->size());
                    outVertices->push_back(vert);
                } else {
                    if (outError) *outError = "OBJ f vertex index out of range: " + v;
                    return false;
                }
            }
            outIndices->push_back(idx[0]);
            outIndices->push_back(idx[1]);
            outIndices->push_back(idx[2]);
        }
    }
    return !outVertices->empty() && !outIndices->empty();
}

}  // namespace

bool ModelLoader::Supports(const std::string& path) const {
    (void)this;
    return SupportsExtension(path);
}

std::type_index ModelLoader::GetResourceType() const {
    return typeid(Mesh);
}

std::any ModelLoader::Load(const std::string& path, ResourceLoadContext& ctx) {
    if (!ctx.device) return {};
    std::string basePath;
    int lodIndex = -1;
    ParseLodPath(path, &basePath, &lodIndex);
    std::string base = PathWithoutLodSuffix(basePath);
    std::unique_ptr<Mesh> mesh;
    if (HasExtension(base, ".gltf") || HasExtension(base, ".glb")) {
        mesh = LoadGLTF(basePath, ctx, lodIndex);
    } else if (HasExtension(base, ".obj")) {
        mesh = LoadOBJ(basePath, ctx);
    }
    if (!mesh) return {};
    return std::any(mesh.release());
}

std::unique_ptr<Mesh> ModelLoader::LoadGLTF(const std::string& path, ResourceLoadContext& ctx, int lodIndex) {
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(LoadImageDataStb, nullptr);
    tinygltf::Model model;
    std::string err, warn;
    bool ok = false;
    if (HasExtension(path, ".glb")) {
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, path.c_str());
    } else {
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, path.c_str());
    }
    if (!ok) {
        if (ctx.resourceManager)
            ctx.resourceManager->SetLastError("tinygltf load failed: " + path + " - " + err);
        return nullptr;
    }

    if (lodIndex >= 0) {
        if (static_cast<size_t>(lodIndex) >= model.meshes.size()) {
            if (ctx.resourceManager)
                ctx.resourceManager->SetLastError("glTF LOD index out of range: " + path);
            return nullptr;
        }
    }

    std::vector<VertexPNT> allVertices;
    std::vector<std::uint32_t> allIndices;
    std::vector<SubMesh> subMeshes;
    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

    const size_t meshStart = (lodIndex >= 0) ? static_cast<size_t>(lodIndex) : 0u;
    const size_t meshEnd   = (lodIndex >= 0) ? static_cast<size_t>(lodIndex) + 1u : model.meshes.size();

    for (size_t mi = meshStart; mi < meshEnd; ++mi) {
        const tinygltf::Mesh& gltfMesh = model.meshes[mi];
        for (const tinygltf::Primitive& prim : gltfMesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode >= 0) continue;

            auto posIt = prim.attributes.find("POSITION");
            if (posIt == prim.attributes.end()) continue;
            int posAcc = posIt->second;
            size_t posStride, vertexCount;
            const unsigned char* posData = GetAccessorData(model, posAcc, &posStride, &vertexCount);
            if (!posData || vertexCount == 0) continue;

            std::vector<VertexPNT> verts(vertexCount);
            for (size_t i = 0; i < vertexCount; ++i) {
                const float* p = reinterpret_cast<const float*>(posData + i * posStride);
                verts[i].px = p[0];
                verts[i].py = p[1];
                verts[i].pz = p[2];
                verts[i].nx = 0.f;
                verts[i].ny = 0.f;
                verts[i].nz = 1.f;
                verts[i].u = 0.f;
                verts[i].v = 0.f;

                boundsMin.x = std::min(boundsMin.x, p[0]);
                boundsMin.y = std::min(boundsMin.y, p[1]);
                boundsMin.z = std::min(boundsMin.z, p[2]);
                boundsMax.x = std::max(boundsMax.x, p[0]);
                boundsMax.y = std::max(boundsMax.y, p[1]);
                boundsMax.z = std::max(boundsMax.z, p[2]);
            }

            auto normIt = prim.attributes.find("NORMAL");
            if (normIt != prim.attributes.end()) {
                glm::vec3* normals = reinterpret_cast<glm::vec3*>(&verts[0].nx);
                ReadVec3Float(model, normIt->second, normals, vertexCount);
            }
            auto uvIt = prim.attributes.find("TEXCOORD_0");
            if (uvIt != prim.attributes.end()) {
                glm::vec2* uvs = reinterpret_cast<glm::vec2*>(&verts[0].u);
                ReadVec2Float(model, uvIt->second, uvs, vertexCount);
            }

            std::uint32_t indexOffset = static_cast<std::uint32_t>(allIndices.size());
            std::uint32_t indexCount = 0;
            std::uint32_t baseVertex = static_cast<std::uint32_t>(allVertices.size());

            if (prim.indices >= 0) {
                std::vector<std::uint32_t> indices;
                ReadIndices(model, prim.indices, &indices);
                for (std::uint32_t idx : indices) {
                    allIndices.push_back(baseVertex + idx);
                }
                indexCount = static_cast<std::uint32_t>(indices.size());
            } else {
                for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(vertexCount); ++i) {
                    allIndices.push_back(baseVertex + i);
                }
                indexCount = static_cast<std::uint32_t>(vertexCount);
            }

            allVertices.insert(allVertices.end(), verts.begin(), verts.end());

            SubMesh sub;
            sub.indexOffset = indexOffset;
            sub.indexCount = indexCount;
            sub.materialIndex = (prim.material >= 0) ? static_cast<std::uint32_t>(prim.material) : 0u;
            subMeshes.push_back(sub);
        }
    }

    if (allVertices.empty() || allIndices.empty()) {
        if (ctx.resourceManager)
            ctx.resourceManager->SetLastError("glTF has no mesh data: " + path);
        return nullptr;
    }

    // 构建材质路径列表：materialPaths[glTF material index] = 供 Load<Material> 使用的路径
    std::string baseDir = GetBaseDir(path);
    std::vector<std::string> materialPaths;
    if (!model.materials.empty()) {
        materialPaths.reserve(model.materials.size());
        for (size_t i = 0; i < model.materials.size(); ++i) {
            const std::string& name = model.materials[i].name;
            std::string matPath = baseDir + "/materials/";
            if (name.empty()) {
                matPath += "material_" + std::to_string(i) + ".json";
            } else {
                matPath += name + ".json";
            }
            materialPaths.push_back(matPath);
        }
    } else {
        // 无材质时保证 materialIndex 0 有效
        materialPaths.push_back("");
    }

    kale_device::BufferDesc vbDesc;
    vbDesc.size = allVertices.size() * sizeof(VertexPNT);
    vbDesc.usage = kale_device::BufferUsage::Vertex;
    vbDesc.cpuVisible = false;

    kale_device::BufferDesc ibDesc;
    ibDesc.size = allIndices.size() * sizeof(std::uint32_t);
    ibDesc.usage = kale_device::BufferUsage::Index;
    ibDesc.cpuVisible = false;

    kale_device::BufferHandle vh;
    kale_device::BufferHandle ih;

    if (ctx.stagingMgr && ctx.device) {
        /* Staging 路径：CreateBuffer(desc, nullptr) + Allocate + memcpy + SubmitUpload + FlushUploads + WaitForFence + Free */
        vh = ctx.device->CreateBuffer(vbDesc, nullptr);
        ih = ctx.device->CreateBuffer(ibDesc, nullptr);
        if (!vh.IsValid() || !ih.IsValid()) {
            if (vh.IsValid()) ctx.device->DestroyBuffer(vh);
            if (ih.IsValid()) ctx.device->DestroyBuffer(ih);
            if (ctx.resourceManager)
                ctx.resourceManager->SetLastError("CreateBuffer failed for: " + path);
            return nullptr;
        }
        const std::size_t vbBytes = allVertices.size() * sizeof(VertexPNT);
        const std::size_t ibBytes = allIndices.size() * sizeof(std::uint32_t);
        StagingAllocation stagingV = ctx.stagingMgr->Allocate(vbBytes);
        StagingAllocation stagingI = ctx.stagingMgr->Allocate(ibBytes);
        if (!stagingV.IsValid() || !stagingI.IsValid()) {
            if (stagingV.IsValid()) ctx.stagingMgr->Free(stagingV);
            if (stagingI.IsValid()) ctx.stagingMgr->Free(stagingI);
            ctx.device->DestroyBuffer(vh);
            ctx.device->DestroyBuffer(ih);
            if (ctx.resourceManager)
                ctx.resourceManager->SetLastError("Staging Allocate failed for: " + path);
            return nullptr;
        }
        std::memcpy(stagingV.mappedPtr, allVertices.data(), vbBytes);
        std::memcpy(stagingI.mappedPtr, allIndices.data(), ibBytes);
        ctx.stagingMgr->SubmitUpload(nullptr, stagingV, vh, 0);
        ctx.stagingMgr->SubmitUpload(nullptr, stagingI, ih, 0);
        kale_device::FenceHandle fence = ctx.stagingMgr->FlushUploads(ctx.device);
        if (fence.IsValid()) {
            ctx.device->WaitForFence(fence);
        }
        ctx.stagingMgr->Free(stagingV);
        ctx.stagingMgr->Free(stagingI);
    } else {
        /* 无 Staging 时回退：直接 CreateBuffer(desc, data) */
        vh = ctx.device->CreateBuffer(vbDesc, allVertices.data());
        ih = ctx.device->CreateBuffer(ibDesc, allIndices.data());
        if (!vh.IsValid() || !ih.IsValid()) {
            if (vh.IsValid()) ctx.device->DestroyBuffer(vh);
            if (ih.IsValid()) ctx.device->DestroyBuffer(ih);
            if (ctx.resourceManager)
                ctx.resourceManager->SetLastError("CreateBuffer failed for: " + path);
            return nullptr;
        }
    }

    auto mesh = std::make_unique<Mesh>();
    mesh->vertexBuffer = vh;
    mesh->indexBuffer = ih;
    mesh->indexCount = static_cast<std::uint32_t>(allIndices.size());
    mesh->vertexCount = static_cast<std::uint32_t>(allVertices.size());
    mesh->topology = kale_device::PrimitiveTopology::TriangleList;
    mesh->bounds.min = boundsMin;
    mesh->bounds.max = boundsMax;
    mesh->subMeshes = std::move(subMeshes);
    mesh->materialPaths = std::move(materialPaths);
    return mesh;
}

std::unique_ptr<Mesh> ModelLoader::LoadOBJ(const std::string& path, ResourceLoadContext& ctx) {
    std::vector<VertexPNT> allVertices;
    std::vector<std::uint32_t> allIndices;
    std::string err;
    if (!ParseOBJ(path, &allVertices, &allIndices, &err)) {
        if (ctx.resourceManager)
            ctx.resourceManager->SetLastError(err.empty() ? "OBJ parse failed: " + path : err);
        return nullptr;
    }

    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
    for (const VertexPNT& v : allVertices) {
        boundsMin.x = std::min(boundsMin.x, v.px);
        boundsMin.y = std::min(boundsMin.y, v.py);
        boundsMin.z = std::min(boundsMin.z, v.pz);
        boundsMax.x = std::max(boundsMax.x, v.px);
        boundsMax.y = std::max(boundsMax.y, v.py);
        boundsMax.z = std::max(boundsMax.z, v.pz);
    }

    SubMesh sub;
    sub.indexOffset = 0;
    sub.indexCount = static_cast<std::uint32_t>(allIndices.size());
    sub.materialIndex = 0;
    std::vector<SubMesh> subMeshes = {sub};
    std::vector<std::string> materialPaths = {""};

    kale_device::BufferDesc vbDesc;
    vbDesc.size = allVertices.size() * sizeof(VertexPNT);
    vbDesc.usage = kale_device::BufferUsage::Vertex;
    vbDesc.cpuVisible = false;
    kale_device::BufferDesc ibDesc;
    ibDesc.size = allIndices.size() * sizeof(std::uint32_t);
    ibDesc.usage = kale_device::BufferUsage::Index;
    ibDesc.cpuVisible = false;

    kale_device::BufferHandle vh;
    kale_device::BufferHandle ih;

    if (ctx.stagingMgr && ctx.device) {
        vh = ctx.device->CreateBuffer(vbDesc, nullptr);
        ih = ctx.device->CreateBuffer(ibDesc, nullptr);
        if (!vh.IsValid() || !ih.IsValid()) {
            if (vh.IsValid()) ctx.device->DestroyBuffer(vh);
            if (ih.IsValid()) ctx.device->DestroyBuffer(ih);
            if (ctx.resourceManager)
                ctx.resourceManager->SetLastError("CreateBuffer failed for OBJ: " + path);
            return nullptr;
        }
        const std::size_t vbBytes = allVertices.size() * sizeof(VertexPNT);
        const std::size_t ibBytes = allIndices.size() * sizeof(std::uint32_t);
        StagingAllocation stagingV = ctx.stagingMgr->Allocate(vbBytes);
        StagingAllocation stagingI = ctx.stagingMgr->Allocate(ibBytes);
        if (!stagingV.IsValid() || !stagingI.IsValid()) {
            if (stagingV.IsValid()) ctx.stagingMgr->Free(stagingV);
            if (stagingI.IsValid()) ctx.stagingMgr->Free(stagingI);
            ctx.device->DestroyBuffer(vh);
            ctx.device->DestroyBuffer(ih);
            if (ctx.resourceManager)
                ctx.resourceManager->SetLastError("Staging Allocate failed for OBJ: " + path);
            return nullptr;
        }
        std::memcpy(stagingV.mappedPtr, allVertices.data(), vbBytes);
        std::memcpy(stagingI.mappedPtr, allIndices.data(), ibBytes);
        ctx.stagingMgr->SubmitUpload(nullptr, stagingV, vh, 0);
        ctx.stagingMgr->SubmitUpload(nullptr, stagingI, ih, 0);
        kale_device::FenceHandle fence = ctx.stagingMgr->FlushUploads(ctx.device);
        if (fence.IsValid())
            ctx.device->WaitForFence(fence);
        ctx.stagingMgr->Free(stagingV);
        ctx.stagingMgr->Free(stagingI);
    } else {
        vh = ctx.device->CreateBuffer(vbDesc, allVertices.data());
        ih = ctx.device->CreateBuffer(ibDesc, allIndices.data());
        if (!vh.IsValid() || !ih.IsValid()) {
            if (vh.IsValid()) ctx.device->DestroyBuffer(vh);
            if (ih.IsValid()) ctx.device->DestroyBuffer(ih);
            if (ctx.resourceManager)
                ctx.resourceManager->SetLastError("CreateBuffer failed for OBJ: " + path);
            return nullptr;
        }
    }

    auto mesh = std::make_unique<Mesh>();
    mesh->vertexBuffer = vh;
    mesh->indexBuffer = ih;
    mesh->indexCount = static_cast<std::uint32_t>(allIndices.size());
    mesh->vertexCount = static_cast<std::uint32_t>(allVertices.size());
    mesh->topology = kale_device::PrimitiveTopology::TriangleList;
    mesh->bounds.min = boundsMin;
    mesh->bounds.max = boundsMax;
    mesh->subMeshes = std::move(subMeshes);
    mesh->materialPaths = std::move(materialPaths);
    return mesh;
}

}  // namespace kale::resource
