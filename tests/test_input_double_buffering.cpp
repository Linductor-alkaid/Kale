/**
 * @file test_input_double_buffering.cpp
 * @brief phase11-11.5 输入状态双缓冲单元测试
 *
 * 验证：当前帧/上一帧状态维护；键盘与鼠标 JustPressed/JustReleased 判断正确。
 */

#include <kale_device/input_manager.hpp>
#include <kale_engine/render_engine.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <cstdlib>
#include <iostream>

using namespace kale_device;
using namespace kale;

#define TEST_CHECK(cond)                                                         \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " " << #cond \
                      << std::endl;                                              \
            std::exit(1);                                                        \
        }                                                                       \
    } while (0)

int main() {
    if (std::getenv("SDL_VIDEODRIVER") == nullptr) {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
    }

    RenderEngine engine;
    RenderEngine::Config config;
    config.width = 320;
    config.height = 240;
    config.title = "InputDoubleBufferingTest";
    config.enableValidation = false;
    if (!engine.Initialize(config)) {
        return 0;
    }

    InputManager* input = engine.GetInputManager();
    TEST_CHECK(input != nullptr);

    // --- 键盘双缓冲与 JustPressed / JustReleased ---
    input->Update();
    TEST_CHECK(!input->IsKeyPressed(KeyCode::A));
    TEST_CHECK(!input->IsKeyJustPressed(KeyCode::A));
    TEST_CHECK(!input->IsKeyJustReleased(KeyCode::A));

    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.type = SDL_EVENT_KEY_DOWN;
    ev.key.scancode = SDL_SCANCODE_A;
    ev.key.windowID = 0;
    TEST_CHECK(SDL_PushEvent(&ev));

    input->Update();
    TEST_CHECK(input->IsKeyPressed(KeyCode::A));
    TEST_CHECK(input->IsKeyJustPressed(KeyCode::A));
    TEST_CHECK(!input->IsKeyJustReleased(KeyCode::A));

    input->Update();
    TEST_CHECK(input->IsKeyPressed(KeyCode::A));
    TEST_CHECK(!input->IsKeyJustPressed(KeyCode::A));
    TEST_CHECK(!input->IsKeyJustReleased(KeyCode::A));

    ev.type = SDL_EVENT_KEY_UP;
    ev.key.type = SDL_EVENT_KEY_UP;
    ev.key.scancode = SDL_SCANCODE_A;
    TEST_CHECK(SDL_PushEvent(&ev));

    input->Update();
    TEST_CHECK(!input->IsKeyPressed(KeyCode::A));
    TEST_CHECK(!input->IsKeyJustPressed(KeyCode::A));
    TEST_CHECK(input->IsKeyJustReleased(KeyCode::A));

    input->Update();
    TEST_CHECK(!input->IsKeyJustReleased(KeyCode::A));

    // --- 鼠标双缓冲与 JustPressed / JustReleased ---
    input->Update();
    TEST_CHECK(!input->IsMouseButtonPressed(MouseButton::Left));
    TEST_CHECK(!input->IsMouseButtonJustPressed(MouseButton::Left));
    TEST_CHECK(!input->IsMouseButtonJustReleased(MouseButton::Left));

    ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.x = 100.0f;
    ev.button.y = 100.0f;
    ev.button.windowID = 0;
    TEST_CHECK(SDL_PushEvent(&ev));

    input->Update();
    TEST_CHECK(input->IsMouseButtonPressed(MouseButton::Left));
    TEST_CHECK(input->IsMouseButtonJustPressed(MouseButton::Left));
    TEST_CHECK(!input->IsMouseButtonJustReleased(MouseButton::Left));

    input->Update();
    TEST_CHECK(input->IsMouseButtonPressed(MouseButton::Left));
    TEST_CHECK(!input->IsMouseButtonJustPressed(MouseButton::Left));
    TEST_CHECK(!input->IsMouseButtonJustReleased(MouseButton::Left));

    ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    ev.button.button = SDL_BUTTON_LEFT;
    ev.button.x = 100.0f;
    ev.button.y = 100.0f;
    ev.button.windowID = 0;
    TEST_CHECK(SDL_PushEvent(&ev));

    input->Update();
    TEST_CHECK(!input->IsMouseButtonPressed(MouseButton::Left));
    TEST_CHECK(!input->IsMouseButtonJustPressed(MouseButton::Left));
    TEST_CHECK(input->IsMouseButtonJustReleased(MouseButton::Left));

    input->Update();
    TEST_CHECK(!input->IsMouseButtonJustReleased(MouseButton::Left));

    return 0;
}
