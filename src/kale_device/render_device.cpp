/**
 * @file render_device.cpp
 * @brief CreateRenderDevice 工厂实现
 */

#include <kale_device/render_device.hpp>
#include <kale_device/vulkan_render_device.hpp>

namespace kale_device {

std::unique_ptr<IRenderDevice> CreateRenderDevice(Backend backend) {
    switch (backend) {
        case Backend::Vulkan:
            return std::make_unique<VulkanRenderDevice>();
        case Backend::OpenGL:
            return nullptr;
        default:
            return nullptr;
    }
}

}  // namespace kale_device
