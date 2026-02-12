// Kale 设备抽象层 - 窗口系统
// 基于 SDL3 的窗口创建、事件循环与 Vulkan/OpenGL 原生句柄

#pragma once

#include <cstdint>
#include <string>

#include <SDL3/SDL_video.h>  // SDL_Window

namespace kale_device {

/// 窗口配置
struct WindowConfig {
    uint32_t width = 1920;
    uint32_t height = 1080;
    std::string title = "Kale";
    bool fullscreen = false;
    bool resizable = true;
};

/// 基于 SDL3 的窗口系统：创建窗口、事件轮询、尺寸查询与 Vulkan Surface 用原生句柄
class WindowSystem {
public:
    WindowSystem() = default;
    ~WindowSystem();

    WindowSystem(const WindowSystem&) = delete;
    WindowSystem& operator=(const WindowSystem&) = delete;

    /// 创建 SDL 窗口。若 SDL 未初始化则先初始化 VIDEO | GAMEPAD | EVENTS
    /// \return 成功返回 true，失败返回 false（可查 SDL_GetError）
    bool Create(const WindowConfig& config);

    /// 销毁窗口；若由本类初始化了 SDL，不调用 Quit（由引擎统一 Quit）
    void Destroy();

    /// 获取 SDL 窗口指针
    SDL_Window* GetWindow() const { return window_; }

    /// 供 Vulkan Surface / OpenGL Context 使用的原生句柄（即 SDL_Window*）
    void* GetNativeHandle() const;

    uint32_t GetWidth() const;
    uint32_t GetHeight() const;

    /// 设置窗口客户区尺寸（逻辑尺寸）
    void Resize(uint32_t width, uint32_t height);

    /// 轮询本帧事件；处理 SDL_EVENT_QUIT / SDL_EVENT_WINDOW_CLOSE_REQUESTED 会置位 shouldClose_
    /// \return true 表示可继续运行，false 表示应退出（已收到退出或关闭请求）
    bool PollEvents();

    /// 是否应关闭（收到 QUIT 或窗口关闭请求后为 true）
    bool ShouldClose() const { return shouldClose_; }

private:
    SDL_Window* window_ = nullptr;
    bool shouldClose_ = false;
    bool sdlInitializedByUs_ = false;  // 本类是否调用了 SDL_Init，用于是否在 Destroy 时 Quit（当前不在 Destroy 调 Quit，仅记是否我们 Init）
};

}  // namespace kale_device
