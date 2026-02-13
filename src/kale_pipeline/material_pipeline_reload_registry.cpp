/**
 * @file material_pipeline_reload_registry.cpp
 * @brief MaterialPipelineReloadRegistry 实现
 */

#include <kale_pipeline/material_pipeline_reload_registry.hpp>
#include <kale_pipeline/material.hpp>
#include <kale_pipeline/shader_manager.hpp>

#include <kale_device/rdi_types.hpp>

namespace kale::pipeline {

void MaterialPipelineReloadRegistry::RegisterMaterial(Material* mat,
                                                      const std::string& vertexPath,
                                                      const std::string& fragmentPath,
                                                      const kale_device::PipelineDesc& descWithoutShaders) {
    if (!mat) return;
    Entry e;
    e.mat = mat;
    e.vertexPath = vertexPath;
    e.fragmentPath = fragmentPath;
    e.descWithoutShaders = descWithoutShaders;
    e.descWithoutShaders.shaders.clear();
    entries_.push_back(std::move(e));
}

void MaterialPipelineReloadRegistry::UnregisterMaterial(Material* mat) {
    if (!mat) return;
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [mat](const Entry& e) { return e.mat == mat; }),
        entries_.end());
}

void MaterialPipelineReloadRegistry::OnShaderReloaded(const std::string& path) {
    if (!shaderManager_ || !device_) return;
    for (const Entry& e : entries_) {
        if (e.vertexPath != path && e.fragmentPath != path) continue;
        Material* mat = e.mat;
        if (!mat) continue;
        kale_device::ShaderHandle vertHandle = shaderManager_->GetShader(
            ShaderManager::MakeCacheKey(e.vertexPath, kale_device::ShaderStage::Vertex));
        kale_device::ShaderHandle fragHandle = shaderManager_->GetShader(
            ShaderManager::MakeCacheKey(e.fragmentPath, kale_device::ShaderStage::Fragment));
        if (!vertHandle.IsValid() || !fragHandle.IsValid()) continue;
        kale_device::PipelineDesc desc = e.descWithoutShaders;
        desc.shaders = {vertHandle, fragHandle};
        kale_device::PipelineHandle newPipeline = device_->CreatePipeline(desc);
        if (!newPipeline.IsValid()) continue;
        kale_device::PipelineHandle oldPipeline = mat->GetPipeline();
        if (oldPipeline.IsValid())
            device_->DestroyPipeline(oldPipeline);
        mat->SetPipeline(newPipeline);
    }
}

}  // namespace kale::pipeline
