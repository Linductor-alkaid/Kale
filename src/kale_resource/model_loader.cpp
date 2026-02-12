/**
 * @file model_loader.cpp
 * @brief ModelLoader 实现：tinygltf 解析 glTF，生成 Mesh（顶点/索引缓冲、bounds、subMeshes）
 */

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <typeindex>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <tiny_gltf.h>

#include <kale_device/rdi_types.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>
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

bool SupportsExtension(const std::string& path) {
    return HasExtension(path, ".gltf") || HasExtension(path, ".glb");
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

}  // namespace

bool ModelLoader::Supports(const std::string& path) const {
    return SupportsExtension(path);
}

std::type_index ModelLoader::GetResourceType() const {
    return typeid(Mesh);
}

std::any ModelLoader::Load(const std::string& path, ResourceLoadContext& ctx) {
    if (!ctx.device) return {};
    auto mesh = LoadGLTF(path, ctx);
    if (!mesh) return {};
    return std::any(mesh.release());
}

std::unique_ptr<Mesh> ModelLoader::LoadGLTF(const std::string& path, ResourceLoadContext& ctx) {
    tinygltf::TinyGLTF loader;
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

    std::vector<VertexPNT> allVertices;
    std::vector<std::uint32_t> allIndices;
    std::vector<SubMesh> subMeshes;
    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

    for (const tinygltf::Mesh& gltfMesh : model.meshes) {
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

    kale_device::BufferDesc vbDesc;
    vbDesc.size = allVertices.size() * sizeof(VertexPNT);
    vbDesc.usage = kale_device::BufferUsage::Vertex;
    vbDesc.cpuVisible = false;

    kale_device::BufferDesc ibDesc;
    ibDesc.size = allIndices.size() * sizeof(std::uint32_t);
    ibDesc.usage = kale_device::BufferUsage::Index;
    ibDesc.cpuVisible = false;

    kale_device::BufferHandle vh = ctx.device->CreateBuffer(vbDesc, allVertices.data());
    kale_device::BufferHandle ih = ctx.device->CreateBuffer(ibDesc, allIndices.data());
    if (!vh.IsValid() || !ih.IsValid()) {
        if (vh.IsValid()) ctx.device->DestroyBuffer(vh);
        if (ih.IsValid()) ctx.device->DestroyBuffer(ih);
        if (ctx.resourceManager)
            ctx.resourceManager->SetLastError("CreateBuffer failed for: " + path);
        return nullptr;
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
    return mesh;
}

}  // namespace kale::resource
