// Kale 资源管理层 - 占位源文件

#include <kale_resource/resource_cache.hpp>
#include <kale_resource/resource_handle.hpp>
#include <kale_resource/resource_loader.hpp>
#include <kale_resource/resource_manager.hpp>

namespace kale::resource {

void placeholder() {
    // 验证句柄类型可实例化与类型擦除
    ResourceHandle<Mesh> mh;
    (void)mh;
    ResourceHandleAny any = ToAny(MeshHandle{1});
    (void)any;

    // 验证 ResourceCache：Register、Get、IsReady、FindByPath、AddRef、Release
    ResourceCache cache;
    Mesh mesh;
    MeshHandle h = cache.Register<Mesh>("mesh/test", &mesh, true);
    (void)h;
    Mesh* p = cache.Get(h);
    (void)p;
    bool ready = cache.IsReady(h);
    (void)ready;
    auto found = cache.FindByPath("mesh/test", typeid(Mesh));
    (void)found;
    cache.AddRef(ToAny(h));
    cache.Release(ToAny(h));
    cache.Release(ToAny(h));
    // 延迟释放：仅清除 any（mesh 为栈对象，不 delete）；条目从 cache 移除
    cache.ProcessPendingReleases([](ResourceHandleAny, std::any& a) { a = std::any(); });

    // 验证 IResourceLoader 接口与 ResourceLoadContext 可编译
    ResourceLoadContext ctx;
    (void)ctx;
    // IResourceLoader 为纯虚基类，由 TextureLoader/ModelLoader 等实现

    // 验证 ResourceManager：构造、SetAssetPath、AddPathAlias、ResolvePath、RegisterLoader、FindLoader
    ResourceManager mgr(nullptr, nullptr, nullptr);
    mgr.SetAssetPath("/data/assets");
    std::string r0 = mgr.ResolvePath("models/box.gltf");
    (void)r0;
    mgr.AddPathAlias("@models", "assets/models");
    std::string r1 = mgr.ResolvePath("@models/box.gltf");
    (void)r1;
    mgr.SetLastError("test error");
    std::string err = mgr.GetLastError();
    (void)err;
    IResourceLoader* loader = mgr.FindLoader("dummy.png", typeid(Texture));
    (void)loader;
}

}  // namespace kale::resource
