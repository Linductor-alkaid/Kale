// Kale 资源管理层 - 占位源文件

#include <kale_resource/resource_handle.hpp>

namespace kale::resource {

void placeholder() {
    // 验证句柄类型可实例化与类型擦除
    ResourceHandle<Mesh> mh;
    (void)mh;
    ResourceHandleAny any = ToAny(MeshHandle{1});
    (void)any;
}

}  // namespace kale::resource
