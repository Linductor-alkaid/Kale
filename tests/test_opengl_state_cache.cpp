/**
 * @file test_opengl_state_cache.cpp
 * @brief phase13-13.7 OpenGL 状态缓存单元测试
 *
 * 验证：重复 Bind 同一资源不崩溃（缓存跳过冗余 GL 调用）；
 * 销毁资源后缓存失效，后续绑定其他资源并 Submit 不崩溃。
 */

#include <kale_device/render_device.hpp>
#include <kale_device/command_list.hpp>
#include <kale_device/rdi_types.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <cassert>
#include <memory>

using namespace kale_device;

int main() {
    std::unique_ptr<IRenderDevice> dev = CreateRenderDevice(Backend::OpenGL);
    if (!dev) {
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
        return 0;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window* window = SDL_CreateWindow("OpenGLStateCache", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
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

    BufferDesc bd;
    bd.size = 64;
    bd.usage = BufferUsage::Vertex;

    BufferHandle buf1 = dev->CreateBuffer(bd, nullptr);
    assert(buf1.IsValid());

    // 重复绑定同一 buffer，状态缓存应跳过冗余 glBindBuffer，不崩溃
    CommandList* cmd = dev->BeginCommandList(0);
    assert(cmd);
    cmd->BindVertexBuffer(0, buf1);
    cmd->BindVertexBuffer(0, buf1);
    dev->EndCommandList(cmd);
    dev->Submit({cmd});

    dev->DestroyBuffer(buf1);

    // 销毁后缓存已失效，再创建并绑定另一 buffer 并 Submit 不崩溃
    BufferHandle buf2 = dev->CreateBuffer(bd, nullptr);
    assert(buf2.IsValid());
    cmd = dev->BeginCommandList(0);
    assert(cmd);
    cmd->BindVertexBuffer(0, buf2);
    dev->EndCommandList(cmd);
    dev->Submit({cmd});
    dev->DestroyBuffer(buf2);

    dev->Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
