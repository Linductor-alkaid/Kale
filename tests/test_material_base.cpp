/**
 * @file test_material_base.cpp
 * @brief phase7-7.7 Material 基类单元测试
 */

#include <kale_pipeline/material.hpp>
#include <kale_resource/resource_types.hpp>
#include <kale_device/rdi_types.hpp>

#include <cstring>
#include <stdexcept>
#include <string>

using namespace kale::resource;
using namespace kale_device;

static void test_default_state() {
    kale::pipeline::Material mat;
    if (mat.GetShader() != nullptr) throw std::runtime_error("default GetShader should be null");
    if (mat.GetPipeline().IsValid()) throw std::runtime_error("default GetPipeline should be invalid");
    if (mat.GetTexture("diffuse") != nullptr) throw std::runtime_error("default GetTexture should be null");
    std::size_t sz = 1;
    if (mat.GetParameter("foo", &sz) != nullptr || sz != 0) throw std::runtime_error("default GetParameter should be null");
}

static void test_set_get_pipeline() {
    kale::pipeline::Material mat;
    PipelineHandle h;
    h.id = 42u;
    mat.SetPipeline(h);
    if (!mat.GetPipeline().IsValid() || mat.GetPipeline().id != 42u)
        throw std::runtime_error("SetPipeline/GetPipeline mismatch");
}

static void test_set_get_shader() {
    kale::pipeline::Material mat;
    Shader shader;
    shader.handle.id = 100u;
    mat.SetShader(&shader);
    if (mat.GetShader() == nullptr || mat.GetShader()->handle.id != 100u)
        throw std::runtime_error("SetShader/GetShader mismatch");
    mat.SetShader(nullptr);
    if (mat.GetShader() != nullptr) throw std::runtime_error("SetShader(nullptr) should clear");
}

static void test_set_get_texture() {
    kale::pipeline::Material mat;
    Texture tex;
    tex.handle.id = 1u;
    mat.SetTexture("albedo", &tex);
    if (mat.GetTexture("albedo") != &tex) throw std::runtime_error("GetTexture(albedo) mismatch");
    if (mat.GetTexture("missing") != nullptr) throw std::runtime_error("GetTexture(missing) should be null");
    mat.SetTexture("", &tex);  // empty name no-op
    if (mat.GetTexture("") != nullptr) throw std::runtime_error("empty name should not store");
}

static void test_set_get_parameter() {
    kale::pipeline::Material mat;
    float value = 3.14f;
    mat.SetParameter("roughness", &value, sizeof(value));
    std::size_t size = 0;
    const void* p = mat.GetParameter("roughness", &size);
    if (p == nullptr || size != sizeof(float)) throw std::runtime_error("GetParameter size mismatch");
    if (std::memcmp(p, &value, sizeof(value)) != 0) throw std::runtime_error("GetParameter data mismatch");
    mat.SetParameter("", &value, sizeof(value));  // empty name no-op
    if (mat.GetParameter("", nullptr) != nullptr) throw std::runtime_error("empty name param should not store");
    mat.SetParameter("zero", nullptr, 0);  // null data no-op
    if (mat.GetParameter("zero", &size) != nullptr) throw std::runtime_error("zero size should not store");
}

static void test_parameter_overwrite() {
    kale::pipeline::Material mat;
    int a = 1, b = 2;
    mat.SetParameter("x", &a, sizeof(a));
    mat.SetParameter("x", &b, sizeof(b));
    std::size_t size = 0;
    const int* got = static_cast<const int*>(mat.GetParameter("x", &size));
    if (!got || size != sizeof(int) || *got != 2) throw std::runtime_error("parameter overwrite should update value");
}

int main() {
    test_default_state();
    test_set_get_pipeline();
    test_set_get_shader();
    test_set_get_texture();
    test_set_get_parameter();
    test_parameter_overwrite();
    return 0;
}
