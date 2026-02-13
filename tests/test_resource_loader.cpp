/**
 * @file test_resource_loader.cpp
 * @brief IResourceLoader 接口与 ResourceLoadContext 单元测试（phase3-3.3）
 */

#include <kale_resource/resource_loader.hpp>
#include <cassert>
#include <string>

using namespace kale::resource;

/** 最小实现：仅用于验证 IResourceLoader 接口 */
class MockLoader : public IResourceLoader {
public:
    explicit MockLoader(const std::string& ext) : ext_(ext) {}

    bool Supports(const std::string& path) const override {
        return path.size() >= ext_.size() &&
               path.compare(path.size() - ext_.size(), ext_.size(), ext_) == 0;
    }

    std::any Load(const std::string& path, ResourceLoadContext& ctx) override {
        (void)path;
        (void)ctx;
        return 42;
    }

    std::type_index GetResourceType() const override { return typeid(int); }

private:
    std::string ext_;
};

int main() {
    ResourceLoadContext ctx;
    assert(ctx.device == nullptr);
    assert(ctx.stagingMgr == nullptr);
    assert(ctx.resourceManager == nullptr);

    MockLoader loader(".mock");
    assert(loader.Supports("a.mock"));
    assert(loader.Supports("/path/to/file.mock"));
    assert(!loader.Supports("a.mock2"));
    assert(!loader.Supports("mock"));

    assert(loader.GetResourceType() == typeid(int));

    std::any result = loader.Load("test.mock", ctx);
    assert(result.has_value());
    assert(std::any_cast<int>(result) == 42);

    return 0;
}
