/**
 * @file test_file_change_detection.cpp
 * @brief phase12-12.1 文件变化侦测单元测试
 *
 * 覆盖：EnableHotReload/IsHotReloadEnabled、ProcessHotReload 禁用时不操作、
 * 已加载资源 path→lastModified 记录、文件 mtime 变化时 ProcessHotReload 触发回调；
 * ForEachLoadedEntry 枚举已加载条目。
 */

#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_cache.hpp>

#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <typeindex>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

namespace {

struct DummyResource {
    int value = 0;
    explicit DummyResource(int v) : value(v) {}
};

class FileBackedLoader : public kale::resource::IResourceLoader {
public:
    explicit FileBackedLoader(std::string pathPrefix) : pathPrefix_(std::move(pathPrefix)) {}

    bool Supports(const std::string& path) const override {
        return path.size() >= pathPrefix_.size() &&
               path.compare(path.size() - pathPrefix_.size(), pathPrefix_.size(), pathPrefix_) == 0;
    }

    std::any Load(const std::string& path,
                  kale::resource::ResourceLoadContext& ctx) override {
        (void)path;
        (void)ctx;
        return std::any(static_cast<DummyResource*>(new DummyResource(1)));
    }

    std::type_index GetResourceType() const override {
        return typeid(DummyResource);
    }

private:
    std::string pathPrefix_;
};

}  // namespace

static void test_enable_and_is_hot_reload() {
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    TEST_CHECK(!rm.IsHotReloadEnabled());
    rm.EnableHotReload(true);
    TEST_CHECK(rm.IsHotReloadEnabled());
    rm.EnableHotReload(false);
    TEST_CHECK(!rm.IsHotReloadEnabled());
}

static void test_process_hot_reload_when_disabled() {
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    rm.EnableHotReload(false);
    rm.ProcessHotReload();
    rm.ProcessHotReload();
}

static void test_for_each_loaded_entry() {
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    rm.RegisterLoader(std::make_unique<FileBackedLoader>("hotreload_test.txt"));
    rm.SetAssetPath("");

    int count = 0;
    rm.GetCache().ForEachLoadedEntry([&count](const std::string&, std::type_index, kale::resource::ResourceHandleAny) {
        ++count;
    });
    TEST_CHECK(count == 0);

    kale::resource::ResourceHandle<DummyResource> h = rm.Load<DummyResource>("/nonexistent/hotreload_test.txt");
    TEST_CHECK(h.IsValid());
    count = 0;
    rm.GetCache().ForEachLoadedEntry([&count](const std::string& path, std::type_index typeId, kale::resource::ResourceHandleAny) {
        ++count;
        TEST_CHECK(path.find("hotreload_test.txt") != std::string::npos);
        TEST_CHECK(typeId == typeid(DummyResource));
    });
    TEST_CHECK(count == 1);
}

static void test_file_mtime_change_triggers_callback() {
    namespace fs = std::filesystem;
    fs::path tmpDir = fs::temp_directory_path() / "kale_hotreload_test";
    fs::create_directories(tmpDir);
    fs::path filePath = tmpDir / "hotreload_test.txt";
    {
        std::ofstream f(filePath);
        TEST_CHECK(f);
        f << "test";
    }
    TEST_CHECK(fs::exists(filePath));

    std::string assetPath = tmpDir.string();
    if (assetPath.back() != '/' && assetPath.back() != '\\')
        assetPath += "/";
    kale::resource::ResourceManager rm(nullptr, nullptr, nullptr);
    rm.RegisterLoader(std::make_unique<FileBackedLoader>("hotreload_test.txt"));
    rm.SetAssetPath(assetPath);

    kale::resource::ResourceHandle<DummyResource> h = rm.Load<DummyResource>("hotreload_test.txt");
    TEST_CHECK(h.IsValid());

    std::vector<std::pair<std::string, std::type_index>> changed;
    rm.RegisterHotReloadCallback([&changed](const std::string& path, std::type_index typeId) {
        changed.emplace_back(path, typeId);
    });

    rm.EnableHotReload(true);
    rm.ProcessHotReload();
    TEST_CHECK(changed.empty());

    fs::file_time_type t0 = fs::last_write_time(filePath);
    fs::file_time_type t1 = t0 + std::chrono::seconds(2);
    try {
        fs::last_write_time(filePath, t1);
    } catch (const fs::filesystem_error&) {
        fs::remove_all(tmpDir);
        return;
    }

    rm.ProcessHotReload();
    TEST_CHECK(changed.size() == 1u);
    TEST_CHECK(changed[0].first.find("hotreload_test.txt") != std::string::npos);
    TEST_CHECK(changed[0].second == typeid(DummyResource));

    changed.clear();
    rm.ProcessHotReload();
    TEST_CHECK(changed.empty());

    fs::remove_all(tmpDir);
}

int main() {
    test_enable_and_is_hot_reload();
    test_process_hot_reload_when_disabled();
    test_for_each_loaded_entry();
    test_file_mtime_change_triggers_callback();
    return 0;
}
