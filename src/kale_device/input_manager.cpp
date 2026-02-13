// Kale 设备抽象层 - 输入管理器实现

#include <kale_device/input_manager.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>
#include <cstdlib>
#include <unordered_map>
#include <variant>

namespace kale_device {

namespace {

struct GamepadCache {
    std::unordered_map<SDL_JoystickID, SDL_Gamepad*> openGamepads;
};

GamepadCache* GetCache(void* p) {
    return static_cast<GamepadCache*>(p);
}

static int ToSDL(GamepadAxis axis) {
    return static_cast<int>(axis);
}
static int ToSDL(GamepadButton button) {
    return static_cast<int>(button);
}

}  // namespace

InputManager::~InputManager() {
    if (gamepadCache_) {
        GamepadCache* c = GetCache(gamepadCache_);
        for (auto& kv : c->openGamepads) {
            if (kv.second) SDL_CloseGamepad(kv.second);
        }
        delete c;
        gamepadCache_ = nullptr;
    }
}

void InputManager::Initialize(SDL_Window* window) {
    window_ = window;
    quitRequested_ = false;
    for (int i = 0; i < kMaxScancodes; ++i) {
        keyCurrent_[i] = false;
        keyPrevious_[i] = false;
    }
    mouseX_ = mouseY_ = 0.0f;
    mouseDeltaX_ = mouseDeltaY_ = 0.0f;
    buttonCurrent_ = buttonPrevious_ = 0;
    mouseWheelDelta_ = 0.0f;
    if (gamepadCache_) {
        GamepadCache* c = GetCache(gamepadCache_);
        for (auto& kv : c->openGamepads) {
            if (kv.second) SDL_CloseGamepad(kv.second);
        }
        c->openGamepads.clear();
    } else {
        gamepadCache_ = new GamepadCache{};
    }
}

void InputManager::Update() {
    // 双缓冲：当前变上一帧
    for (int i = 0; i < kMaxScancodes; ++i) {
        keyPrevious_[i] = keyCurrent_[i];
    }
    buttonPrevious_ = buttonCurrent_;
    mouseWheelDelta_ = 0.0f;

    SDL_PumpEvents();

    auto dispatch = [this](InputEventType t, const InputEvent& e) {
        auto it = eventCallbacks_.find(t);
        if (it == eventCallbacks_.end()) return;
        for (const InputEventCallback& cb : it->second) {
            if (cb) cb(e);
        }
    };

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        InputEvent ev;
        switch (event.type) {
            case SDL_EVENT_QUIT:
                quitRequested_ = true;
                ev.type = InputEventType::Quit;
                dispatch(InputEventType::Quit, ev);
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                ev.windowId = static_cast<int>(event.window.windowID);
                if (window_ && event.window.windowID == SDL_GetWindowID(window_)) {
                    quitRequested_ = true;
                }
                ev.type = InputEventType::WindowCloseRequested;
                dispatch(InputEventType::WindowCloseRequested, ev);
                break;
            case SDL_EVENT_KEY_DOWN:
                ev.type = InputEventType::KeyDown;
                ev.key = static_cast<KeyCode>(event.key.scancode);
                dispatch(InputEventType::KeyDown, ev);
                break;
            case SDL_EVENT_KEY_UP:
                ev.type = InputEventType::KeyUp;
                ev.key = static_cast<KeyCode>(event.key.scancode);
                dispatch(InputEventType::KeyUp, ev);
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                ev.type = InputEventType::MouseButtonDown;
                ev.mouseButton = static_cast<MouseButton>(event.button.button);
                ev.mousePosition = glm::vec2(event.button.x, event.button.y);
                dispatch(InputEventType::MouseButtonDown, ev);
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                ev.type = InputEventType::MouseButtonUp;
                ev.mouseButton = static_cast<MouseButton>(event.button.button);
                ev.mousePosition = glm::vec2(event.button.x, event.button.y);
                dispatch(InputEventType::MouseButtonUp, ev);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                ev.type = InputEventType::MouseMotion;
                ev.mousePosition = glm::vec2(event.motion.x, event.motion.y);
                ev.mouseDelta = glm::vec2(event.motion.xrel, event.motion.yrel);
                dispatch(InputEventType::MouseMotion, ev);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                mouseWheelDelta_ += event.wheel.y;
                ev.type = InputEventType::MouseWheel;
                ev.wheelDelta = static_cast<float>(event.wheel.y);
                ev.mousePosition = glm::vec2(event.wheel.mouse_x, event.wheel.mouse_y);
                dispatch(InputEventType::MouseWheel, ev);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED: {
                ev.type = InputEventType::GamepadRemoved;
                ev.gamepadInstanceId = static_cast<int>(event.gdevice.which);
                dispatch(InputEventType::GamepadRemoved, ev);
                if (gamepadCache_) {
                    SDL_JoystickID id = event.gdevice.which;
                    GamepadCache* c = GetCache(gamepadCache_);
                    auto it = c->openGamepads.find(id);
                    if (it != c->openGamepads.end()) {
                        if (it->second) SDL_CloseGamepad(it->second);
                        c->openGamepads.erase(it);
                    }
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_ADDED:
                ev.type = InputEventType::GamepadAdded;
                ev.gamepadInstanceId = static_cast<int>(event.gdevice.which);
                dispatch(InputEventType::GamepadAdded, ev);
                break;
            default:
                break;
        }
    }

    int numKeys = 0;
    const bool* keyState = SDL_GetKeyboardState(&numKeys);
    for (int i = 0; i < kMaxScancodes && i < numKeys; ++i) {
        keyCurrent_[i] = keyState[i];
    }

    float mx = 0.0f, my = 0.0f;
    buttonCurrent_ = static_cast<std::uint32_t>(SDL_GetMouseState(&mx, &my));
    mouseX_ = mx;
    mouseY_ = my;

    float rdx = 0.0f, rdy = 0.0f;
    SDL_GetRelativeMouseState(&rdx, &rdy);
    mouseDeltaX_ = rdx;
    mouseDeltaY_ = rdy;
}

bool InputManager::IsKeyPressed(KeyCode key) const {
    int sc = static_cast<int>(key);
    if (sc < 0 || sc >= kMaxScancodes) return false;
    return keyCurrent_[sc];
}

bool InputManager::IsKeyJustPressed(KeyCode key) const {
    int sc = static_cast<int>(key);
    if (sc < 0 || sc >= kMaxScancodes) return false;
    return keyCurrent_[sc] && !keyPrevious_[sc];
}

bool InputManager::IsKeyJustReleased(KeyCode key) const {
    int sc = static_cast<int>(key);
    if (sc < 0 || sc >= kMaxScancodes) return false;
    return !keyCurrent_[sc] && keyPrevious_[sc];
}

glm::vec2 InputManager::GetMousePosition() const {
    return glm::vec2(mouseX_, mouseY_);
}

glm::vec2 InputManager::GetMouseDelta() const {
    return glm::vec2(mouseDeltaX_, mouseDeltaY_);
}

bool InputManager::IsMouseButtonPressed(MouseButton button) const {
    std::uint32_t mask = 1u << (static_cast<std::uint32_t>(button) - 1);
    return (buttonCurrent_ & mask) != 0;
}

float InputManager::GetMouseWheelDelta() const {
    return mouseWheelDelta_;
}

bool InputManager::IsGamepadConnected(int index) const {
    if (index < 0) return false;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return false;
    bool ok = (index < count);
    SDL_free(ids);
    return ok;
}

float InputManager::GetGamepadAxis(int index, GamepadAxis axis) const {
    if (index < 0 || !gamepadCache_) return 0.0f;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids || index >= count) {
        if (ids) SDL_free(ids);
        return 0.0f;
    }
    SDL_JoystickID instanceId = ids[index];
    SDL_free(ids);

    GamepadCache* c = GetCache(gamepadCache_);
    SDL_Gamepad* gp = nullptr;
    auto it = c->openGamepads.find(instanceId);
    if (it != c->openGamepads.end()) {
        gp = it->second;
    } else {
        gp = SDL_OpenGamepad(instanceId);
        if (gp) c->openGamepads[instanceId] = gp;
    }
    if (!gp) return 0.0f;

    SDL_GamepadAxis sdlAxis = static_cast<SDL_GamepadAxis>(ToSDL(axis));
    if (sdlAxis == SDL_GAMEPAD_AXIS_INVALID || sdlAxis >= SDL_GAMEPAD_AXIS_COUNT) return 0.0f;
    Sint16 raw = SDL_GetGamepadAxis(gp, sdlAxis);
    if (axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger) {
        return raw <= 0 ? 0.0f : static_cast<float>(raw) / 32767.0f;
    }
    return raw == 0 ? 0.0f : static_cast<float>(raw) / 32767.0f;
}

bool InputManager::IsGamepadButtonPressed(int index, GamepadButton button) const {
    if (index < 0 || !gamepadCache_) return false;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids || index >= count) {
        if (ids) SDL_free(ids);
        return false;
    }
    SDL_JoystickID instanceId = ids[index];
    SDL_free(ids);

    GamepadCache* c = GetCache(gamepadCache_);
    SDL_Gamepad* gp = nullptr;
    auto it = c->openGamepads.find(instanceId);
    if (it != c->openGamepads.end()) {
        gp = it->second;
    } else {
        gp = SDL_OpenGamepad(instanceId);
        if (gp) c->openGamepads[instanceId] = gp;
    }
    if (!gp) return false;

    SDL_GamepadButton sdlBtn = static_cast<SDL_GamepadButton>(ToSDL(button));
    if (sdlBtn == SDL_GAMEPAD_BUTTON_INVALID || sdlBtn >= SDL_GAMEPAD_BUTTON_COUNT) return false;
    return SDL_GetGamepadButton(gp, sdlBtn);
}

bool InputManager::IsMouseButtonJustPressed(MouseButton button) const {
    std::uint32_t mask = 1u << (static_cast<std::uint32_t>(button) - 1);
    return (buttonCurrent_ & mask) != 0 && (buttonPrevious_ & mask) == 0;
}

bool InputManager::IsMouseButtonJustReleased(MouseButton button) const {
    std::uint32_t mask = 1u << (static_cast<std::uint32_t>(button) - 1);
    return (buttonCurrent_ & mask) == 0 && (buttonPrevious_ & mask) != 0;
}

void InputManager::AddActionBinding(const std::string& action, const InputBinding& binding) {
    actionBindings_[action].push_back(binding);
}

void InputManager::ClearActionBindings(const std::string& action) {
    actionBindings_.erase(action);
}

bool InputManager::IsActionTriggered(const std::string& action) const {
    auto it = actionBindings_.find(action);
    if (it == actionBindings_.end()) return false;
    for (const InputBinding& b : it->second) {
        if (std::holds_alternative<KeyCode>(b)) {
            if (IsKeyJustPressed(std::get<KeyCode>(b))) return true;
        } else if (std::holds_alternative<MouseButton>(b)) {
            if (IsMouseButtonJustPressed(std::get<MouseButton>(b))) return true;
        } else {
            const GamepadBinding& gb = std::get<GamepadBinding>(b);
            if (std::holds_alternative<GamepadButton>(gb.input)) {
                if (IsGamepadButtonPressed(gb.gamepadIndex, std::get<GamepadButton>(gb.input))) return true;
            }
            // 轴不参与 Trigger，仅用于 GetActionValue
        }
    }
    return false;
}

float InputManager::GetActionValue(const std::string& action) const {
    auto it = actionBindings_.find(action);
    if (it == actionBindings_.end()) return 0.0f;
    float value = 0.0f;
    for (const InputBinding& b : it->second) {
        if (std::holds_alternative<KeyCode>(b)) {
            if (IsKeyPressed(std::get<KeyCode>(b))) return 1.0f;
        } else if (std::holds_alternative<MouseButton>(b)) {
            if (IsMouseButtonPressed(std::get<MouseButton>(b))) return 1.0f;
        } else {
            const GamepadBinding& gb = std::get<GamepadBinding>(b);
            if (std::holds_alternative<GamepadButton>(gb.input)) {
                if (IsGamepadButtonPressed(gb.gamepadIndex, std::get<GamepadButton>(gb.input))) return 1.0f;
            } else {
                float v = GetGamepadAxis(gb.gamepadIndex, std::get<GamepadAxis>(gb.input));
                if (v != 0.0f) value = v;  // 轴值覆盖，多轴时取最后一个非零
            }
        }
    }
    return value;
}

void InputManager::RegisterCallback(InputEventType type, InputEventCallback callback) {
    if (callback) eventCallbacks_[type].push_back(std::move(callback));
}

void InputManager::ClearCallbacks(InputEventType type) {
    eventCallbacks_.erase(type);
}

void InputManager::ClearAllCallbacks() {
    eventCallbacks_.clear();
}

}  // namespace kale_device
