/**
 * @file test_material_loader.cpp
 * @brief phase8-8.2 MaterialLoader 单元测试
 *
 * 覆盖：Supports(.json)、GetResourceType；Load 解析 JSON 标量/纹理键；
 * 空文件或非法 JSON 失败；通过 ResourceManager::Load<Material> 集成。
 */

#include <kale_pipeline/material_loader.hpp>
#include <kale_pipeline/pbr_material.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <typeindex>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                               \
    } while (0)

int main() {
    using namespace kale::resource;
    using namespace kale::pipeline;

    // Supports: .json true, other false
    MaterialLoader loader;
    TEST_CHECK(loader.Supports("mat.json"));
    TEST_CHECK(loader.Supports("/path/to/foo.json"));
    TEST_CHECK(!loader.Supports("mat.png"));
    TEST_CHECK(!loader.Supports("mat"));

    // GetResourceType
    TEST_CHECK(loader.GetResourceType() == typeid(kale::resource::Material));

    // ResourceManager 注册 MaterialLoader，无 device/TextureLoader 时仅测标量
    ResourceManager mgr(nullptr, nullptr, nullptr);
    mgr.SetAssetPath("/tmp");
    mgr.RegisterLoader(std::make_unique<MaterialLoader>());

    // 临时 JSON 文件：仅标量（路径相对 assetPath /tmp）
    std::string jsonFile = "kale_test_material_loader.json";
    std::string jsonFullPath = "/tmp/" + jsonFile;
    {
        std::ofstream f(jsonFullPath);
        TEST_CHECK(f);
        f << R"({"metallic": 0.2, "roughness": 0.8})";
        f.close();
    }

    MaterialHandle handle = mgr.Load<kale::resource::Material>(jsonFile);
    TEST_CHECK(handle.IsValid());
    kale::resource::Material* baseMat = mgr.Get(handle);
    TEST_CHECK(baseMat != nullptr);

    auto* pbr = dynamic_cast<PBRMaterial*>(baseMat);
    TEST_CHECK(pbr != nullptr);
    TEST_CHECK(pbr->GetMetallic() == 0.2f);
    TEST_CHECK(pbr->GetRoughness() == 0.8f);

    // 无纹理键时 GetAlbedo 等为 nullptr
    TEST_CHECK(pbr->GetAlbedo() == nullptr);
    TEST_CHECK(pbr->GetNormal() == nullptr);

    // 非法 JSON：应失败
    std::string badFile = "kale_test_material_loader_bad.json";
    {
        std::ofstream f("/tmp/" + badFile);
        TEST_CHECK(f);
        f << "{ invalid json ";
        f.close();
    }
    MaterialHandle badHandle = mgr.Load<kale::resource::Material>(badFile);
    TEST_CHECK(!badHandle.IsValid());
    TEST_CHECK(!mgr.GetLastError().empty());

    // 空文件：应失败
    std::string emptyFile = "kale_test_material_loader_empty.json";
    {
        std::ofstream f("/tmp/" + emptyFile);
        TEST_CHECK(f);
        f.close();
    }
    MaterialHandle emptyHandle = mgr.Load<kale::resource::Material>(emptyFile);
    TEST_CHECK(!emptyHandle.IsValid());

    // 不存在的路径：ResolvePath 后为 /tmp/nonexistent/material.json，读文件失败
    MaterialHandle missingHandle = mgr.Load<kale::resource::Material>("nonexistent/material.json");
    TEST_CHECK(!missingHandle.IsValid());

    return 0;
}
