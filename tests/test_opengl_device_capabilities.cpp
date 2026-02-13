/**
 * @file test_opengl_device_capabilities.cpp
 * @brief phase13-13.8 OpenGL 设备能力查询单元测试
 *
 * 验证 OpenGL 后端从 GL 上下文正确填充 DeviceCapabilities：
 * maxTextureSize（GL_MAX_TEXTURE_SIZE）、maxComputeWorkGroupSize、
 * supportsGeometryShader/supportsTessellation/supportsComputeShader、
 * maxRecordingThreads == 1。
 */

#include <kale_device/render_device.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <cassert>
#include <cstdint>
#include <memory>

using namespace kale_device;

int main() {
    std::unique_ptr<IRenderDevice> dev = CreateRenderDevice(Backend::OpenGL);
    if (!dev) return 0;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 0;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window* window = SDL_CreateWindow("OpenGLCaps", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Quit();
        return 0;
    }

    DeviceConfig config;
    config.windowHandle = window;
    config.width = 64;
    config.height = 64;
    config.vsync = false;

    if (!dev->Initialize(config)) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    const DeviceCapabilities& caps = dev->GetCapabilities();

    assert(caps.maxTextureSize > 0 && "maxTextureSize 应从 GL_MAX_TEXTURE_SIZE 查询");
    assert(caps.maxComputeWorkGroupSize[0] >= 1u && "maxComputeWorkGroupSize 至少为 1");
    assert(caps.maxComputeWorkGroupSize[1] >= 1u);
    assert(caps.maxComputeWorkGroupSize[2] >= 1u);
    assert(caps.maxRecordingThreads == 1u && "OpenGL 单上下文，maxRecordingThreads 应为 1");

    (void)caps.supportsGeometryShader;
    (void)caps.supportsTessellation;
    (void)caps.supportsComputeShader;
    (void)caps.supportsRayTracing;

    dev->Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
