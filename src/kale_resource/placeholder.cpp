// Kale 资源管理层 - 占位源文件

#include <kale_resource/resource_cache.hpp>
#include <kale_resource/resource_handle.hpp>

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
}

}  // namespace kale::resource
