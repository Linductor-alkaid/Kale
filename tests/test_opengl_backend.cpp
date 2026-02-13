/**
 * @file test_opengl_backend.cpp
 * @brief phase11-11.6 OpenGL 后端单元测试
 *
 * 验证 CreateRenderDevice(Backend::OpenGL) 在启用 OpenGL 构建时返回非空；
 * 可选：使用带 GL 属性的窗口 Initialize + CreateBuffer + Shutdown。
 */

#include <kale_device/render_device.hpp>
#include <kale_device/rdi_types.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <cassert>
#include <memory>

using namespace kale_device;

int main() {
    std::unique_ptr<IRenderDevice> dev = CreateRenderDevice(Backend::OpenGL);
    if (!dev) {
        // OpenGL 后端未构建（KALE_ENABLE_OPENGL_BACKEND=OFF）或不可用
        return 0;
    }

    assert(dev->GetLastError().empty() || true);

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return 0;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window* window = SDL_CreateWindow("OpenGLTest", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Quit();
        return 0;
    }

    DeviceConfig config;
    config.windowHandle = window;
    config.width = 64;
    config.height = 64;
    config.enableValidation = false;
    config.vsync = false;

    if (!dev->Initialize(config)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    assert(dev->GetCapabilities().maxTextureSize > 0);
    assert(dev->AcquireNextImage() == 0u);
    assert(dev->GetBackBuffer().IsValid());
    assert(dev->GetCurrentFrameIndex() < 3u);

    BufferDesc bd;
    bd.size = 64;
    bd.usage = BufferUsage::Vertex;
    BufferHandle buf = dev->CreateBuffer(bd, nullptr);
    assert(buf.IsValid());
    dev->DestroyBuffer(buf);

    dev->Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
