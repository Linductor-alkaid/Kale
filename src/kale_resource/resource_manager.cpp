/**
 * @file resource_manager.cpp
 * @brief ResourceManager 实现：构造、Loader 注册、路径解析
 */

#include <kale_resource/resource_manager.hpp>

#include <algorithm>
#include <cctype>

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

}  // namespace kale::resource
