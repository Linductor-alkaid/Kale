/**
 * @file material_loader.cpp
 * @brief MaterialLoader 实现：JSON 解析、依赖纹理加载、创建 PBRMaterial
 *
 * phase13-13.14：纹理路径解析（相对材质文件目录或 assetPath）、循环依赖检测。
 */

#include <kale_pipeline/material_loader.hpp>
#include <kale_pipeline/pbr_material.hpp>
#include <kale_pipeline/shader_manager.hpp>

#include <kale_device/rdi_types.hpp>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>

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

/** 当前正在加载的材质路径集合，用于检测材质→材质循环依赖（thread_local 以支持多线程加载） */
thread_local std::unordered_set<std::string> g_loading_material_paths;

/** 判断路径是否为绝对路径（仅 Unix / 或 Windows 盘符） */
bool IsAbsolutePath(const std::string& path) {
    if (path.empty()) return false;
#if defined(_WIN32)
    if (path.size() >= 2u && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':')
        return true;
#endif
    return path[0] == '/';
}

/** 将纹理路径解析为可传给 Load 的路径：若 texPath 相对则相对材质文件所在目录 */
std::string ResolveTexturePath(const std::string& materialPath,
                               const std::string& texPath,
                               kale::resource::ResourceManager* resourceManager) {
    if (!resourceManager || texPath.empty()) return texPath;
    if (IsAbsolutePath(texPath)) return resourceManager->ResolvePath(texPath);
    const size_t lastSlash = materialPath.find_last_of("/\\");
    if (lastSlash == std::string::npos) return resourceManager->ResolvePath(texPath);
    const std::string baseDir = materialPath.substr(0, lastSlash + 1);
    return resourceManager->ResolvePath(baseDir + texPath);
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

    // 循环依赖检测：若当前 path 已在加载栈中则说明存在材质→材质循环（phase13-13.14）
    {
        auto it = g_loading_material_paths.find(path);
        if (it != g_loading_material_paths.end()) {
            ctx.resourceManager->SetLastError("MaterialLoader: circular material dependency: " + path);
            return nullptr;
        }
        g_loading_material_paths.insert(path);
    }
    struct LoadingGuard {
        const std::string& path;
        ~LoadingGuard() { g_loading_material_paths.erase(path); }
    } guard{path};

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

    // 同步加载依赖纹理；纹理路径相对材质文件目录或 assetPath 解析（phase13-13.14）
    auto setTextureFromKey = [&j, &ctx, &mat, &path](const std::string& key,
                                                     void (PBRMaterial::*setter)(kale::resource::Texture*)) {
        auto it = j.find(key);
        if (it == j.end() || !it->is_string()) return;
        std::string texPath = it->get<std::string>();
        if (texPath.empty()) return;
        const std::string resolvedTexPath = ResolveTexturePath(path, texPath, ctx.resourceManager);
        kale::resource::TextureHandle th =
            ctx.resourceManager->Load<kale::resource::Texture>(resolvedTexPath);
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

    // phase13-13.15：可选 shader_vert/shaders_frag + ctx.shaderManager 时创建 Pipeline，供材质热重载后使用新着色器
    auto itVert = j.find("shader_vert");
    auto itFrag = j.find("shader_frag");
    if (ctx.device && ctx.shaderManager && itVert != j.end() && itVert->is_string()
        && itFrag != j.end() && itFrag->is_string()) {
        auto* shaderMgr = static_cast<kale::pipeline::ShaderManager*>(ctx.shaderManager);
        std::string vertPath = ctx.resourceManager->ResolvePath(itVert->get<std::string>());
        std::string fragPath = ctx.resourceManager->ResolvePath(itFrag->get<std::string>());
        kale_device::ShaderHandle vh = shaderMgr->LoadShader(vertPath, kale_device::ShaderStage::Vertex, ctx.device);
        kale_device::ShaderHandle fh = shaderMgr->LoadShader(fragPath, kale_device::ShaderStage::Fragment, ctx.device);
        if (vh.IsValid() && fh.IsValid()) {
            kale_device::PipelineDesc desc;
            desc.shaders = {vh, fh};
            desc.vertexBindings = {{0, 32, false}};  // VertexPNT 8 floats
            desc.vertexAttributes = {
                {0, 0, kale_device::Format::RGB32F, 0},
                {1, 0, kale_device::Format::RGB32F, 12},
                {2, 0, kale_device::Format::RG32F, 24},
            };
            desc.topology = kale_device::PrimitiveTopology::TriangleList;
            desc.rasterization.cullEnable = true;
            desc.rasterization.frontFaceCCW = true;
            desc.depthStencil.depthTestEnable = true;
            desc.depthStencil.depthWriteEnable = true;
            desc.colorAttachmentFormats = {kale_device::Format::RGBA8_SRGB};
            desc.depthAttachmentFormat = kale_device::Format::D24S8;
            kale_device::PipelineHandle ph = ctx.device->CreatePipeline(desc);
            if (ph.IsValid())
                mat->SetPipeline(ph);
        }
    }

    return mat.release();
}

}  // namespace kale::pipeline
