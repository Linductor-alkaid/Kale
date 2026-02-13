/**
 * @file test_pbr_material.cpp
 * @brief phase7-7.11 PBRMaterial 单元测试
 */

#include <kale_pipeline/pbr_material.hpp>
#include <kale_resource/resource_types.hpp>

#include <stdexcept>

using namespace kale::resource;
using namespace kale::pipeline;

static void test_default_pbr() {
    PBRMaterial mat;
    if (mat.GetAlbedo() != nullptr) throw std::runtime_error("default GetAlbedo should be null");
    if (mat.GetNormal() != nullptr) throw std::runtime_error("default GetNormal should be null");
    if (mat.GetAO() != nullptr) throw std::runtime_error("default GetAO should be null");
    if (mat.GetEmissive() != nullptr) throw std::runtime_error("default GetEmissive should be null");
    if (mat.GetMetallic() != 0.0f) throw std::runtime_error("default GetMetallic should be 0");
    if (mat.GetRoughness() != 0.5f) throw std::runtime_error("default GetRoughness should be 0.5");
}

static void test_set_get_textures() {
    PBRMaterial mat;
    Texture albedo, normal, ao, emissive;
    albedo.handle.id = 1u;
    normal.handle.id = 2u;
    ao.handle.id = 3u;
    emissive.handle.id = 4u;

    mat.SetAlbedo(&albedo);
    mat.SetNormal(&normal);
    mat.SetAO(&ao);
    mat.SetEmissive(&emissive);

    if (mat.GetAlbedo() != &albedo) throw std::runtime_error("GetAlbedo mismatch");
    if (mat.GetNormal() != &normal) throw std::runtime_error("GetNormal mismatch");
    if (mat.GetAO() != &ao) throw std::runtime_error("GetAO mismatch");
    if (mat.GetEmissive() != &emissive) throw std::runtime_error("GetEmissive mismatch");
}

static void test_set_get_metallic_roughness() {
    PBRMaterial mat;
    mat.SetMetallic(0.3f);
    mat.SetRoughness(0.7f);
    if (mat.GetMetallic() != 0.3f) throw std::runtime_error("GetMetallic mismatch");
    if (mat.GetRoughness() != 0.7f) throw std::runtime_error("GetRoughness mismatch");

    mat.SetMetallic(0.0f);
    mat.SetRoughness(1.0f);
    if (mat.GetMetallic() != 0.0f) throw std::runtime_error("GetMetallic 0 mismatch");
    if (mat.GetRoughness() != 1.0f) throw std::runtime_error("GetRoughness 1 mismatch");
}

static void test_inherits_material() {
    PBRMaterial mat;
    if (mat.GetMaterialDescriptorSet().IsValid()) throw std::runtime_error("PBR default descriptor set should be invalid");
    mat.ReleaseFrameResources();  // no-op, should not crash
}

int main() {
    test_default_pbr();
    test_set_get_textures();
    test_set_get_metallic_roughness();
    test_inherits_material();
    return 0;
}
