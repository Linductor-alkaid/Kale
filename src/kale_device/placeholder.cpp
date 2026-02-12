// Kale 设备抽象层 - 占位源文件
// 实现时用实际源文件替换

#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

namespace kale_device {

void placeholder() {
    (void)CreateRenderDevice(Backend::Vulkan);
}

}  // namespace kale_device
