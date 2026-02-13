// Kale 设备抽象层 - 输入管理
// 基于 SDL3 的键盘、鼠标状态与每帧轮询（KeyCode/MouseButton 与 SDL 映射）

#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

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

/// 手柄轴，与 SDL_GamepadAxis 语义一致
enum class GamepadAxis : int {
    LeftX = 0,
    LeftY = 1,
    RightX = 2,
    RightY = 3,
    LeftTrigger = 4,
    RightTrigger = 5,
};

/// 手柄按钮，与 SDL_GamepadButton 语义一致（South=Ａ、East=Ｂ、West=Ｘ、North=Ｙ 等）
enum class GamepadButton : int {
    South = 0,
    East = 1,
    West = 2,
    North = 3,
    Back = 4,
    Guide = 5,
    Start = 6,
    LeftStick = 7,
    RightStick = 8,
    LeftShoulder = 9,
    RightShoulder = 10,
    DpadUp = 11,
    DpadDown = 12,
    DpadLeft = 13,
    DpadRight = 14,
};

/// 手柄绑定：gamepadIndex + 按钮或轴
struct GamepadBinding {
    int gamepadIndex = 0;
    std::variant<GamepadButton, GamepadAxis> input;
};

/// 输入绑定：键盘键、鼠标按钮或手柄绑定之一
using InputBinding = std::variant<KeyCode, MouseButton, GamepadBinding>;

/// 便捷构造：键盘键
inline InputBinding Keyboard(KeyCode key) { return key; }
/// 便捷构造：鼠标按钮
inline InputBinding Mouse(MouseButton btn) { return btn; }
/// 便捷构造：手柄按钮（idx 为连接序号）
inline InputBinding GamepadButtonBinding(int idx, GamepadButton button) {
    return GamepadBinding{idx, button};
}
/// 便捷构造：手柄轴（idx 为连接序号）
inline InputBinding GamepadAxisBinding(int idx, GamepadAxis axisValue) {
    return GamepadBinding{idx, axisValue};
}

/// 输入管理器：键盘、鼠标状态与 JustPressed/JustReleased 双缓冲，以及手柄支持
class InputManager {
public:
    InputManager() = default;
    ~InputManager();

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

    /// 手柄：index 为当前连接列表中的序号（0=第一个手柄）
    bool IsGamepadConnected(int index) const;
    /// 手柄：轴值归一化，摇杆约 [-1,1]，扳机 [0,1]
    float GetGamepadAxis(int index, GamepadAxis axis) const;
    /// 手柄：指定按钮是否按下
    bool IsGamepadButtonPressed(int index, GamepadButton button) const;

    /// 动作映射：为 action 添加一个绑定（同一 action 可多绑定）
    void AddActionBinding(const std::string& action, const InputBinding& binding);
    /// 动作映射：清除指定 action 的全部绑定
    void ClearActionBindings(const std::string& action);
    /// 动作映射：任一绑定“触发”则 true（键/鼠为 JustPressed，手柄为当前按下）
    bool IsActionTriggered(const std::string& action) const;
    /// 动作映射：轴值，按键/按钮为 1.0 或 0.0，轴为 [-1,1] 或 [0,1]
    float GetActionValue(const std::string& action) const;

    /// 本帧是否收到退出或窗口关闭请求（由 Update 轮询时设置）
    bool QuitRequested() const { return quitRequested_; }

private:
    bool IsMouseButtonJustPressed(MouseButton button) const;

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

    /// 手柄：已打开实例缓存（实现内为 map instance_id -> SDL_Gamepad*），热插拔时在 Update 中维护
    void* gamepadCache_ = nullptr;

    /// action 名称 -> 绑定列表（同一 action 多绑定）
    std::unordered_map<std::string, std::vector<InputBinding>> actionBindings_;
};

}  // namespace kale_device
