/**
 * @file shader_manager.cpp
 * @brief ShaderManager 实现
 */

#include <kale_pipeline/shader_manager.hpp>
#include <kale_resource/shader_compiler.hpp>

#include <type_traits>

namespace kale::pipeline {

namespace {

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

}  // namespace kale::pipeline
