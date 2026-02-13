/**
 * @file render_device.cpp
 * @brief CreateRenderDevice 工厂实现
 */

#include <kale_device/render_device.hpp>
#include <kale_device/vulkan_render_device.hpp>
#if defined(KALE_HAS_OPENGL_BACKEND)
#include <kale_device/opengl_render_device.hpp>
#endif

namespace kale_device {

std::unique_ptr<IRenderDevice> CreateRenderDevice(Backend backend) {
    switch (backend) {
        case Backend::Vulkan:
            return std::make_unique<VulkanRenderDevice>();
        case Backend::OpenGL:
#if defined(KALE_HAS_OPENGL_BACKEND)
            return std::make_unique<OpenGLRenderDevice>();
#else
            return nullptr;
#endif
        default:
            return nullptr;
    }
}

}  // namespace kale_device
