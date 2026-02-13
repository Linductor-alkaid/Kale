/**
 * @file material_loader.cpp
 * @brief MaterialLoader 实现：JSON 解析、依赖纹理加载、创建 PBRMaterial
 */

#include <kale_pipeline/material_loader.hpp>
#include <kale_pipeline/pbr_material.hpp>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>

namespace kale::pipeline {

namespace {

bool HasJsonExtension(const std::string& path) {
    if (path.size() < 5u) return false;
    return path.compare(path.size() - 5, 5, ".json") == 0;
}

std::string ReadFileToString(const std::string& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
}

}  // namespace

bool MaterialLoader::Supports(const std::string& path) const {
    return HasJsonExtension(path);
}

std::type_index MaterialLoader::GetResourceType() const {
    return typeid(kale::resource::Material);
}

std::any MaterialLoader::Load(const std::string& path, kale::resource::ResourceLoadContext& ctx) {
    kale::resource::Material* mat = LoadJSON(path, ctx);
    if (!mat) return {};
    return std::any(mat);
}

kale::resource::Material* MaterialLoader::LoadJSON(const std::string& path,
                                                  kale::resource::ResourceLoadContext& ctx) {
    if (!ctx.resourceManager) return nullptr;

    std::string content = ReadFileToString(path);
    if (content.empty()) {
        ctx.resourceManager->SetLastError("MaterialLoader: failed to read file: " + path);
        return nullptr;
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(content);
    } catch (const nlohmann::json::exception& e) {
        ctx.resourceManager->SetLastError(std::string("MaterialLoader: JSON parse error: ") + e.what());
        return nullptr;
    }

    if (!j.is_object()) {
        ctx.resourceManager->SetLastError("MaterialLoader: root must be a JSON object");
        return nullptr;
    }

    auto mat = std::make_unique<PBRMaterial>();

    auto setTextureFromKey = [&j, &ctx, &mat](const std::string& key,
                                               void (PBRMaterial::*setter)(kale::resource::Texture*)) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_string()) return;
        std::string texPath = it->get<std::string>();
        if (texPath.empty()) return;
        kale::resource::TextureHandle th = ctx.resourceManager->Load<kale::resource::Texture>(texPath);
        kale::resource::Texture* tex = ctx.resourceManager->Get(th);
        if (tex) (mat.get()->*setter)(tex);
    };

    setTextureFromKey("albedo", &PBRMaterial::SetAlbedo);
    setTextureFromKey("normal", &PBRMaterial::SetNormal);
    setTextureFromKey("ao", &PBRMaterial::SetAO);
    setTextureFromKey("emissive", &PBRMaterial::SetEmissive);

    // 标量：metallic, roughness
    auto setFloat = [&j, &mat](const std::string& key, void (PBRMaterial::*setter)(float)) {
        auto it = j.find(key);
        if (it == j.end()) return;
        if (it->is_number()) {
            float v = it->get<float>();
            (mat.get()->*setter)(v);
        }
    };
    setFloat("metallic", &PBRMaterial::SetMetallic);
    setFloat("roughness", &PBRMaterial::SetRoughness);

    return mat.release();
}

}  // namespace kale::pipeline
