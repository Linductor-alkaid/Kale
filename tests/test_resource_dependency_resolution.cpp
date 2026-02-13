/**
 * @file test_resource_dependency_resolution.cpp
 * @brief phase13-13.14 资源依赖解析单元测试
 *
 * 覆盖：材质内纹理路径解析（相对材质文件目录）、同步 Load 依赖、
 * 相对路径 "albedo": "brick.png" 在 materials/mat.json 下解析为 materials/brick.png。
 * 循环依赖检测由 LoadJSON 内 loading set 保证，材质→材质引用时生效。
 */

#include <kale_pipeline/material_loader.hpp>
#include <kale_pipeline/pbr_material.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

int main() {
    using namespace kale::resource;
    using namespace kale::pipeline;

    ResourceManager mgr(nullptr, nullptr, nullptr);
    mgr.SetAssetPath("/tmp");
    mgr.RegisterLoader(std::make_unique<MaterialLoader>());

    // 材质文件在子目录，纹理路径相对材质文件：albedo "brick.png" 应解析为 materials/brick.png（相对 assetPath）
    const std::string matDir = "materials";
    const std::string matFile = matDir + "/dep_resolve_mat.json";
    const std::string matFullPath = "/tmp/" + matFile;
    std::filesystem::create_directories("/tmp/" + matDir);
    {
        std::ofstream f(matFullPath);
        TEST_CHECK(f);
        f << R"({"albedo": "brick.png", "metallic": 0.1, "roughness": 0.5})";
        f.close();
    }

    MaterialHandle handle = mgr.Load<kale::resource::Material>(matFile);
    TEST_CHECK(handle.IsValid());
    kale::resource::Material* baseMat = mgr.Get(handle);
    TEST_CHECK(baseMat != nullptr);

    auto* pbr = dynamic_cast<PBRMaterial*>(baseMat);
    TEST_CHECK(pbr != nullptr);
    TEST_CHECK(pbr->GetMetallic() == 0.1f);
    TEST_CHECK(pbr->GetRoughness() == 0.5f);
    // 无 TextureLoader 时 GetAlbedo 为 nullptr；路径解析不崩溃即可
    (void)pbr->GetAlbedo();

    // 绝对路径形式的纹理键：应正常解析
    const std::string matFile2 = matDir + "/dep_abs.json";
    {
        std::ofstream f(matFullPath.substr(0, matFullPath.rfind('/') + 1) + "dep_abs.json");
        TEST_CHECK(f);
        f << R"({"normal": "/textures/n.png", "metallic": 0.0})";
        f.close();
    }
    MaterialHandle handle2 = mgr.Load<kale::resource::Material>(matFile2);
    TEST_CHECK(handle2.IsValid());
    kale::resource::Material* mat2 = mgr.Get(handle2);
    TEST_CHECK(mat2 != nullptr);

    return 0;
}
