/**
 * @file test_refcount_deferred_release.cpp
 * @brief phase12-12.3 引用计数与延迟释放单元测试
 *
 * 覆盖：refCount=0 加入待释放队列、ProcessPendingReleases 统一销毁、
 * Unload(ResourceHandleAny)、重复 Release 幂等、ProcessPendingReleases 后 Get 返回 nullptr。
 */

#include <kale_resource/resource_cache.hpp>
#include <kale_resource/resource_handle.hpp>
#include <kale_resource/resource_manager.hpp>
#include <kale_resource/resource_types.hpp>

#include <cstdlib>
#include <iostream>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

namespace {

using namespace kale::resource;

void test_cache_refcount_zero_adds_to_pending() {
    ResourceCache cache;
    Mesh* heapMesh = new Mesh();
    MeshHandle h = cache.Register<Mesh>("path/mesh", heapMesh, true);
    TEST_CHECK(h.IsValid());
    TEST_CHECK(cache.Get(h) == heapMesh);

    cache.Release(ToAny(h));
    // 未调用 ProcessPendingReleases 前，条目仍在但 path 已从 pathToId_ 移除
    auto found = cache.FindByPath("path/mesh", typeid(Mesh));
    TEST_CHECK(!found.has_value());

    int callbackCount = 0;
    cache.ProcessPendingReleases([&callbackCount, heapMesh](ResourceHandleAny, std::any& a) {
        ++callbackCount;
        Mesh* p = std::any_cast<Mesh*>(a);
        TEST_CHECK(p == heapMesh);
        delete p;
        a = std::any();
    });
    TEST_CHECK(callbackCount == 1);
    TEST_CHECK(cache.Get(h) == nullptr);
}

void test_cache_release_twice_idempotent() {
    ResourceCache cache;
    Mesh* heapMesh = new Mesh();
    MeshHandle h = cache.Register<Mesh>("path/mesh2", heapMesh, true);
    cache.Release(ToAny(h));
    cache.Release(ToAny(h));
    int callbackCount = 0;
    cache.ProcessPendingReleases([&callbackCount, heapMesh](ResourceHandleAny, std::any& a) {
        ++callbackCount;
        Mesh* p = std::any_cast<Mesh*>(a);
        TEST_CHECK(p == heapMesh);
        delete p;
        a = std::any();
    });
    TEST_CHECK(callbackCount == 1);
}

void test_cache_addref_prevents_pending() {
    ResourceCache cache;
    Mesh* heapMesh = new Mesh();
    MeshHandle h = cache.Register<Mesh>("path/mesh3", heapMesh, true);
    cache.AddRef(ToAny(h));
    cache.Release(ToAny(h));
    int callbackCount = 0;
    cache.ProcessPendingReleases([&callbackCount](ResourceHandleAny, std::any&) { ++callbackCount; });
    TEST_CHECK(callbackCount == 0);
    cache.Release(ToAny(h));
    cache.ProcessPendingReleases([&callbackCount, heapMesh](ResourceHandleAny, std::any& a) {
        ++callbackCount;
        delete std::any_cast<Mesh*>(a);
        a = std::any();
    });
    TEST_CHECK(callbackCount == 1);
}

void test_manager_unload_and_process_pending() {
    ResourceManager mgr(nullptr, nullptr, nullptr);
    mgr.SetAssetPath("/assets");
    Mesh* heapMesh = new Mesh();
    MeshHandle h = mgr.GetCache().Register<Mesh>("/assets/models/box.gltf", heapMesh, true);
    TEST_CHECK(mgr.Get<Mesh>(h) == heapMesh);

    mgr.Unload(ToAny(h));
    TEST_CHECK(!mgr.GetCache().FindByPath("/assets/models/box.gltf", typeid(Mesh)).has_value());

    mgr.ProcessPendingReleases();
    TEST_CHECK(mgr.Get<Mesh>(h) == nullptr);
}

void test_manager_unload_texture_material() {
    ResourceManager mgr(nullptr, nullptr, nullptr);
    mgr.SetAssetPath("/assets");
    Texture* heapTex = new Texture();
    Material* heapMat = new Material();
    TextureHandle th = mgr.GetCache().Register<Texture>("/assets/tex.png", heapTex, true);
    MaterialHandle mh = mgr.GetCache().Register<Material>("/assets/mat.json", heapMat, true);

    mgr.Unload(ToAny(th));
    mgr.Unload(ToAny(mh));
    mgr.ProcessPendingReleases();

    TEST_CHECK(mgr.Get<Texture>(th) == nullptr);
    TEST_CHECK(mgr.Get<Material>(mh) == nullptr);
}

}  // namespace

int main() {
    test_cache_refcount_zero_adds_to_pending();
    test_cache_release_twice_idempotent();
    test_cache_addref_prevents_pending();
    test_manager_unload_and_process_pending();
    test_manager_unload_texture_material();
    return 0;
}
