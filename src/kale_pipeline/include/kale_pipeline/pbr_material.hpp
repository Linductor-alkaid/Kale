/**
 * @file pbr_material.hpp
 * @brief PBR 材质：Albedo、Normal、Metallic、Roughness、AO、Emissive
 *
 * 与 rendering_pipeline_layer_todolist 2.5、phase7-7.11 对齐。
 */

#pragma once

#include <kale_pipeline/material.hpp>
#include <kale_resource/resource_types.hpp>

namespace kale::pipeline {

/**
 * PBR 材质子类，提供固定语义的纹理与标量参数。
 * - 纹理：albedo, normal, ao, emissive（通过基类 SetTexture 绑定）
 * - 标量：metallic, roughness（通过基类 SetParameter 存储）
 */
class PBRMaterial : public Material {
public:
    PBRMaterial() = default;
    ~PBRMaterial() override = default;

    void SetAlbedo(kale::resource::Texture* tex) { SetTexture("albedo", tex); }
    void SetNormal(kale::resource::Texture* tex) { SetTexture("normal", tex); }
    void SetAO(kale::resource::Texture* tex) { SetTexture("ao", tex); }
    void SetEmissive(kale::resource::Texture* tex) { SetTexture("emissive", tex); }

    void SetMetallic(float value) { SetParameter("metallic", &value, sizeof(float)); }
    void SetRoughness(float value) { SetParameter("roughness", &value, sizeof(float)); }

    kale::resource::Texture* GetAlbedo() const { return GetTexture("albedo"); }
    kale::resource::Texture* GetNormal() const { return GetTexture("normal"); }
    kale::resource::Texture* GetAO() const { return GetTexture("ao"); }
    kale::resource::Texture* GetEmissive() const { return GetTexture("emissive"); }

    float GetMetallic() const {
        std::size_t size = 0;
        const void* p = GetParameter("metallic", &size);
        if (p && size >= sizeof(float)) return *static_cast<const float*>(p);
        return 0.0f;
    }
    float GetRoughness() const {
        std::size_t size = 0;
        const void* p = GetParameter("roughness", &size);
        if (p && size >= sizeof(float)) return *static_cast<const float*>(p);
        return 0.5f;
    }
};

}  // namespace kale::pipeline
