// Kale 设备抽象层 - 输入管理
// 基于 SDL3 的键盘、鼠标状态与每帧轮询（KeyCode/MouseButton 与 SDL 映射）

#pragma once

#include <cstdint>
#include <glm/vec2.hpp>

struct SDL_Window;

namespace kale_device {

/// 键盘按键码，与 SDL_Scancode 数值一致，便于直接索引 SDL_GetKeyboardState
enum class KeyCode : int {
    Unknown = 0,
    A = 4,
    B = 5,
    C = 6,
    D = 7,
    E = 8,
    F = 9,
    G = 10,
    H = 11,
    I = 12,
    J = 13,
    K = 14,
    L = 15,
    M = 16,
    N = 17,
    O = 18,
    P = 19,
    Q = 20,
    R = 21,
    S = 22,
    T = 23,
    U = 24,
    V = 25,
    W = 26,
    X = 27,
    Y = 28,
    Z = 29,
    Num1 = 30,
    Num2 = 31,
    Num3 = 32,
    Num4 = 33,
    Num5 = 34,
    Num6 = 35,
    Num7 = 36,
    Num8 = 37,
    Num9 = 38,
    Num0 = 39,
    Return = 40,
    Escape = 41,
    Backspace = 42,
    Tab = 43,
    Space = 44,
    Minus = 45,
    Equals = 46,
    LeftBracket = 47,
    RightBracket = 48,
    Backslash = 49,
    Semicolon = 51,
    Apostrophe = 52,
    Grave = 53,
    Comma = 54,
    Period = 55,
    Slash = 56,
    CapsLock = 57,
    F1 = 58,
    F2 = 59,
    F3 = 60,
    F4 = 61,
    F5 = 62,
    F6 = 63,
    F7 = 64,
    F8 = 65,
    F9 = 66,
    F10 = 67,
    F11 = 68,
    F12 = 69,
    PrintScreen = 70,
    ScrollLock = 71,
    Pause = 72,
    Insert = 73,
    Home = 74,
    PageUp = 75,
    Delete = 76,
    End = 77,
    PageDown = 78,
    Right = 79,
    Left = 80,
    Down = 81,
    Up = 82,
    NumLockClear = 83,
};

/// 鼠标按钮，与 SDL_BUTTON_* 数值一致
enum class MouseButton : std::uint32_t {
    Left = 1,
    Middle = 2,
    Right = 3,
    X1 = 4,
    X2 = 5,
};

/// 输入管理器：键盘、鼠标状态与 JustPressed/JustReleased 双缓冲
class InputManager {
public:
    InputManager() = default;
    ~InputManager() = default;

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    /// 使用 SDL 窗口初始化（用于相对坐标与关闭请求判断）
    void Initialize(SDL_Window* window);

    /// 每帧调用：轮询 SDL 事件、更新键盘/鼠标状态与滚轮增量
    void Update();

    /// 键盘：当前帧是否按下
    bool IsKeyPressed(KeyCode key) const;
    /// 键盘：本帧刚按下（上一帧未按）
    bool IsKeyJustPressed(KeyCode key) const;
    /// 键盘：本帧刚释放（上一帧按下）
    bool IsKeyJustReleased(KeyCode key) const;

    /// 鼠标：窗口内坐标（左上为原点）
    glm::vec2 GetMousePosition() const;
    /// 鼠标：相对移动量（自上一帧）
    glm::vec2 GetMouseDelta() const;
    /// 鼠标：是否按下指定按钮
    bool IsMouseButtonPressed(MouseButton button) const;
    /// 鼠标：本帧滚轮增量（垂直：正为远离用户）
    float GetMouseWheelDelta() const;

    /// 本帧是否收到退出或窗口关闭请求（由 Update 轮询时设置）
    bool QuitRequested() const { return quitRequested_; }

private:
    SDL_Window* window_ = nullptr;
    bool quitRequested_ = false;

    static constexpr int kMaxScancodes = 512;
    bool keyCurrent_[kMaxScancodes] = {};
    bool keyPrevious_[kMaxScancodes] = {};

    float mouseX_ = 0.0f;
    float mouseY_ = 0.0f;
    float mouseDeltaX_ = 0.0f;
    float mouseDeltaY_ = 0.0f;
    std::uint32_t buttonCurrent_ = 0;
    std::uint32_t buttonPrevious_ = 0;
    float mouseWheelDelta_ = 0.0f;
};

}  // namespace kale_device
