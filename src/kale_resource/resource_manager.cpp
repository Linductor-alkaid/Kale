/**
 * @file resource_manager.cpp
 * @brief ResourceManager 实现：构造、Loader 注册、路径解析、占位符
 */

#include <kale_resource/resource_manager.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>

#include <kale_device/rdi_types.hpp>

namespace kale::resource {

namespace {

/** 路径是否为绝对路径（Unix / 或 Windows 盘符） */
bool isAbsolutePath(const std::string& path) {
    if (path.empty()) return false;
#ifdef _WIN32
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
        return true;
#endif
    return path[0] == '/';
}

/** 规范化目录路径：末尾无 / 则不加，有则保留；空则返回 "" */
std::string ensureTrailingSlash(const std::string& path) {
    if (path.empty()) return "";
    return path.back() == '/' ? path : path + "/";
}

// 占位符 Mesh 顶点格式：与 ModelLoader 一致（位置 3 + 法线 3 + UV 2）
struct PlaceholderVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

}  // namespace

ResourceManager::ResourceManager(kale::executor::RenderTaskScheduler* scheduler,
                                 kale_device::IRenderDevice* device,
                                 StagingMemoryManager* stagingMgr)
    : scheduler_(scheduler), device_(device), stagingMgr_(stagingMgr) {}

void ResourceManager::RegisterLoader(std::unique_ptr<IResourceLoader> loader) {
    if (loader) {
        loaders_.push_back(std::move(loader));
    }
}

IResourceLoader* ResourceManager::FindLoader(const std::string& path, std::type_index typeId) {
    const std::string resolved = ResolvePath(path);
    for (auto& p : loaders_) {
        if (p->Supports(resolved) && p->GetResourceType() == typeId) {
            return p.get();
        }
    }
    return nullptr;
}

void ResourceManager::SetAssetPath(const std::string& path) {
    assetPath_ = ensureTrailingSlash(path);
}

void ResourceManager::AddPathAlias(const std::string& alias, const std::string& path) {
    if (alias.empty()) return;
    pathAliases_[alias] = path;
}

std::string ResourceManager::ResolvePath(const std::string& path) const {
    if (path.empty()) return assetPath_;

    std::string current = path;

    // 应用别名：若路径以某 alias 开头，则替换为对应 path
    for (const auto& [alias, target] : pathAliases_) {
        if (alias.empty()) continue;
        bool prefixMatch = current.size() >= alias.size() &&
                           current.compare(0, alias.size(), alias) == 0 &&
                           (current.size() == alias.size() || current[alias.size()] == '/');
        if (prefixMatch) {
            size_t skip = (current.size() > alias.size() && current[alias.size()] == '/')
                              ? alias.size() + 1
                              : alias.size();
            current = ensureTrailingSlash(target) + current.substr(skip);
            break;
        }
    }

    // 相对路径：前面加上 assetPath_
    if (!isAbsolutePath(current)) {
        current = assetPath_ + current;
    }
    return current;
}

std::string ResourceManager::GetLastError() const {
    return lastError_;
}

void ResourceManager::SetLastError(const std::string& message) {
    lastError_ = message;
}

void ResourceManager::RegisterLoadedCallback(LoadedCallback cb) {
    if (cb) {
        std::lock_guard<std::mutex> lock(loadedMutex_);
        loadedCallbacks_.push_back(std::move(cb));
    }
}

void ResourceManager::ProcessLoadedCallbacks() {
    std::vector<std::pair<ResourceHandleAny, std::string>> local;
    std::vector<LoadedCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(loadedMutex_);
        local = std::move(pendingLoaded_);
        pendingLoaded_.clear();
        callbacks = loadedCallbacks_;
    }
    for (const auto& [handle, path] : local) {
        for (const auto& cb : callbacks) {
            if (cb) cb(handle, path);
        }
    }
}

void ResourceManager::EnqueueLoaded(ResourceHandleAny handle, const std::string& path) {
    std::lock_guard<std::mutex> lock(loadedMutex_);
    pendingLoaded_.emplace_back(handle, path);
}

void ResourceManager::CreatePlaceholders() {
    if (!device_) return;

    using kale_device::BufferDesc;
    using kale_device::BufferHandle;
    using kale_device::BufferUsage;
    using kale_device::Format;
    using kale_device::PrimitiveTopology;
    using kale_device::TextureDesc;
    using kale_device::TextureUsage;

    // 占位符 Mesh：单三角形（与 ModelLoader 顶点格式一致）
    {
        PlaceholderVertex vertices[3] = {
            { 0.f,  1.f, 0.f,  0.f, 0.f, 1.f,  0.5f, 0.f },
            { -1.f, -1.f, 0.f,  0.f, 0.f, 1.f,  0.f,  1.f },
            { 1.f, -1.f, 0.f,  0.f, 0.f, 1.f,  1.f,  1.f },
        };
        std::uint32_t indices[3] = {0, 1, 2};

        BufferDesc vbDesc;
        vbDesc.size = sizeof(vertices);
        vbDesc.usage = BufferUsage::Vertex;
        vbDesc.cpuVisible = false;
        BufferHandle vb = device_->CreateBuffer(vbDesc, vertices);
        if (!vb.IsValid()) return;

        BufferDesc ibDesc;
        ibDesc.size = sizeof(indices);
        ibDesc.usage = BufferUsage::Index;
        ibDesc.cpuVisible = false;
        BufferHandle ib = device_->CreateBuffer(ibDesc, indices);
        if (!ib.IsValid()) {
            device_->DestroyBuffer(vb);
            return;
        }

        auto mesh = std::make_unique<Mesh>();
        mesh->vertexBuffer = vb;
        mesh->indexBuffer = ib;
        mesh->indexCount = 3;
        mesh->vertexCount = 3;
        mesh->topology = PrimitiveTopology::TriangleList;
        mesh->bounds.min = glm::vec3(-1.f, -1.f, 0.f);
        mesh->bounds.max = glm::vec3(1.f, 1.f, 0.f);
        mesh->subMeshes.push_back(SubMesh{0, 3, 0});
        placeholderMesh_ = std::move(mesh);
    }

    // 占位符 Texture：1x1 灰色 (RGBA8)
    {
        std::uint8_t pixel[4] = {128, 128, 128, 255};
        TextureDesc texDesc;
        texDesc.width = 1;
        texDesc.height = 1;
        texDesc.depth = 1;
        texDesc.mipLevels = 1;
        texDesc.arrayLayers = 1;
        texDesc.format = Format::RGBA8_UNORM;
        texDesc.usage = TextureUsage::Sampled;
        texDesc.isCube = false;
        kale_device::TextureHandle rdiTex = device_->CreateTexture(texDesc, pixel);
        if (!rdiTex.IsValid()) return;

        auto tex = std::make_unique<Texture>();
        tex->handle = rdiTex;
        tex->width = 1;
        tex->height = 1;
        tex->format = Format::RGBA8_UNORM;
        tex->mipLevels = 1;
        placeholderTexture_ = std::move(tex);
    }

    // 占位符 Material：默认空材质（后续 phase 补充参数与纹理）
    placeholderMaterial_ = std::make_unique<Material>();
}

Mesh* ResourceManager::GetPlaceholderMesh() {
    return placeholderMesh_.get();
}

Texture* ResourceManager::GetPlaceholderTexture() {
    return placeholderTexture_.get();
}

Material* ResourceManager::GetPlaceholderMaterial() {
    return placeholderMaterial_.get();
}

}  // namespace kale::resource
