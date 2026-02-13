/**
 * @file shader_manager.cpp
 * @brief ShaderManager 实现
 */

#include <kale_pipeline/shader_manager.hpp>
#include <kale_resource/shader_compiler.hpp>

#include <optional>
#include <filesystem>
#include <type_traits>

namespace kale::pipeline {

namespace {

std::optional<std::filesystem::file_time_type> GetFileModificationTime(const std::string& path) {
    try {
        std::filesystem::path p(path);
        if (!std::filesystem::exists(p) || !std::filesystem::is_regular_file(p))
            return std::nullopt;
        return std::filesystem::last_write_time(p);
    } catch (...) {
        return std::nullopt;
    }
}

const char* kStageSuffixes[] = {
    "Vertex", "Fragment", "Compute", "Geometry", "TessControl", "TessEvaluation"
};

}  // namespace

std::string ShaderManager::StageSuffix(kale_device::ShaderStage stage) {
    auto i = static_cast<std::underlying_type_t<kale_device::ShaderStage>>(stage);
    if (i < 0 || i >= static_cast<int>(sizeof(kStageSuffixes) / sizeof(kStageSuffixes[0])))
        return "Vertex";
    return kStageSuffixes[i];
}

kale_device::ShaderStage ShaderManager::StageFromSuffix(const std::string& suffix) {
    for (int i = 0; i < static_cast<int>(sizeof(kStageSuffixes) / sizeof(kStageSuffixes[0])); ++i)
        if (suffix == kStageSuffixes[i])
            return static_cast<kale_device::ShaderStage>(i);
    return kale_device::ShaderStage::Vertex;
}

std::string ShaderManager::MakeCacheKey(const std::string& path, kale_device::ShaderStage stage) {
    return path + "|" + StageSuffix(stage);
}

kale_device::ShaderHandle ShaderManager::LoadShader(const std::string& path,
                                                    kale_device::ShaderStage stage,
                                                    kale_device::IRenderDevice* device) {
    lastError_.clear();
    if (!device) {
        SetLastError("ShaderManager::LoadShader: device is null");
        return kale_device::ShaderHandle{};
    }
    if (!compiler_) {
        SetLastError("ShaderManager::LoadShader: compiler is null");
        return kale_device::ShaderHandle{};
    }
    const std::string key = MakeCacheKey(path, stage);
    auto it = shaders_.find(key);
    if (it != shaders_.end() && it->second.IsValid())
        return it->second;
    std::string resolved = compiler_->ResolvePath(path);
    kale_device::ShaderHandle handle = compiler_->Compile(resolved, stage, device);
    if (!handle.IsValid()) {
        SetLastError(compiler_->GetLastError());
        return kale_device::ShaderHandle{};
    }
    if (it != shaders_.end()) {
        device->DestroyShader(it->second);
        it->second = handle;
    } else {
        shaders_[key] = handle;
    }
    RecordPathLastModified(path);
    return handle;
}

kale_device::ShaderHandle ShaderManager::GetShader(const std::string& name) const {
    auto it = shaders_.find(name);
    if (it == shaders_.end())
        return kale_device::ShaderHandle{};
    return it->second;
}

void ShaderManager::ReloadShader(const std::string& path) {
    lastError_.clear();
    if (!compiler_ || !device_) {
        SetLastError("ShaderManager::ReloadShader: compiler or device not set");
        return;
    }
    const std::string prefix = path + "|";
    for (auto& [key, handle] : shaders_) {
        if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0)
            continue;
        kale_device::ShaderStage stage = StageFromSuffix(key.substr(prefix.size()));
        if (handle.IsValid())
            device_->DestroyShader(handle);
        handle = compiler_->Recompile(compiler_->ResolvePath(path), stage, device_);
    }
}

void ShaderManager::RecordPathLastModified(const std::string& path) {
    if (!compiler_) return;
    std::string resolved = compiler_->ResolvePath(path);
    auto mt = GetFileModificationTime(resolved);
    if (!mt) return;
    std::lock_guard<std::mutex> lock(hotReloadMutex_);
    pathLastModified_[path] = *mt;
}

void ShaderManager::EnableHotReload(bool enable) {
    hotReloadEnabled_ = enable;
}

void ShaderManager::ProcessHotReload() {
    if (!hotReloadEnabled_ || !compiler_) return;
    std::vector<std::string> toReload;
    {
        std::lock_guard<std::mutex> lock(hotReloadMutex_);
        for (const auto& [key, handle] : shaders_) {
            (void)handle;
            size_t pos = key.find('|');
            if (pos == std::string::npos) continue;
            std::string path = key.substr(0, pos);
            std::string resolved = compiler_->ResolvePath(path);
            auto current = GetFileModificationTime(resolved);
            if (!current) continue;
            auto it = pathLastModified_.find(path);
            if (it == pathLastModified_.end()) {
                pathLastModified_[path] = *current;
                continue;
            }
            if (*current != it->second) {
                it->second = *current;
                toReload.push_back(path);
            }
        }
    }
    for (const std::string& path : toReload) {
        ReloadShader(path);
        std::vector<ShaderReloadedCallback> callbacks;
        {
            std::lock_guard<std::mutex> lock(hotReloadMutex_);
            callbacks = reloadCallbacks_;
        }
        for (const auto& cb : callbacks) {
            if (cb) cb(path);
        }
    }
}

void ShaderManager::RegisterReloadCallback(ShaderReloadedCallback cb) {
    if (cb) {
        std::lock_guard<std::mutex> lock(hotReloadMutex_);
        reloadCallbacks_.push_back(std::move(cb));
    }
}

}  // namespace kale::pipeline
