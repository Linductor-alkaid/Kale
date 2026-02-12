// Kale 设备抽象层 - 窗口系统实现

#include <kale_device/window_system.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>

namespace kale_device {

namespace {

constexpr uint32_t kSDLInitFlags = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS;

}  // namespace

WindowSystem::~WindowSystem() {
    Destroy();
}

bool WindowSystem::Create(const WindowConfig& config) {
    if (window_) {
        return true;  // 已创建
    }
    if ((SDL_WasInit(0) & kSDLInitFlags) != kSDLInitFlags) {
        if (!SDL_Init(kSDLInitFlags)) {
            return false;
        }
        sdlInitializedByUs_ = true;
    }
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN;  // 供 Vulkan Surface 使用
    if (config.resizable) {
        flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_RESIZABLE);
    }
    if (config.fullscreen) {
        flags = static_cast<SDL_WindowFlags>(flags | SDL_WINDOW_FULLSCREEN);
    }
    SDL_Window* win = SDL_CreateWindow(
        config.title.c_str(),
        static_cast<int>(config.width),
        static_cast<int>(config.height),
        flags);
    if (!win) {
        if (sdlInitializedByUs_) {
            SDL_Quit();
            sdlInitializedByUs_ = false;
        }
        return false;
    }
    window_ = win;
    shouldClose_ = false;
    return true;
}

void WindowSystem::Destroy() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    shouldClose_ = false;
    // 不在此处 SDL_Quit()，由引擎或应用统一退出时调用
    sdlInitializedByUs_ = false;
}

void* WindowSystem::GetNativeHandle() const {
    return static_cast<void*>(window_);
}

uint32_t WindowSystem::GetWidth() const {
    if (!window_) return 0;
    int w = 0, h = 0;
    if (!SDL_GetWindowSize(window_, &w, &h)) return 0;
    return static_cast<uint32_t>(w > 0 ? w : 0);
}

uint32_t WindowSystem::GetHeight() const {
    if (!window_) return 0;
    int w = 0, h = 0;
    if (!SDL_GetWindowSize(window_, &w, &h)) return 0;
    return static_cast<uint32_t>(h > 0 ? h : 0);
}

void WindowSystem::Resize(uint32_t width, uint32_t height) {
    if (window_) {
        SDL_SetWindowSize(window_, static_cast<int>(width), static_cast<int>(height));
    }
}

bool WindowSystem::PollEvents() {
    if (!window_) return true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            shouldClose_ = true;
            return false;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            if (event.window.windowID == SDL_GetWindowID(window_)) {
                shouldClose_ = true;
                return false;
            }
        }
    }
    return !shouldClose_;
}

}  // namespace kale_device
