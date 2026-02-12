// Kale 设备抽象层 - 输入管理器实现

#include <kale_device/input_manager.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

namespace kale_device {

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
}

void InputManager::Update() {
    // 双缓冲：当前变上一帧
    for (int i = 0; i < kMaxScancodes; ++i) {
        keyPrevious_[i] = keyCurrent_[i];
    }
    buttonPrevious_ = buttonCurrent_;
    mouseWheelDelta_ = 0.0f;

    SDL_PumpEvents();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                quitRequested_ = true;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (window_ && event.window.windowID == SDL_GetWindowID(window_)) {
                    quitRequested_ = true;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                mouseWheelDelta_ += event.wheel.y;
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

}  // namespace kale_device
