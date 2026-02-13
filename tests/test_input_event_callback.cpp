/**
 * @file test_input_event_callback.cpp
 * @brief phase11-11.4 输入事件回调单元测试
 *
 * 验证：InputEventType、InputEvent、RegisterCallback、Update 中派发；
 * ClearCallbacks/ClearAllCallbacks；KeyDown/Quit 等类型派发正确。
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
    config.title = "InputEventCallbackTest";
    config.enableValidation = false;
    if (!engine.Initialize(config)) {
        return 0;
    }

    InputManager* input = engine.GetInputManager();
    TEST_CHECK(input != nullptr);

    int keyDownCount = 0;
    KeyCode lastKey = KeyCode::Unknown;
    input->RegisterCallback(InputEventType::KeyDown, [&keyDownCount, &lastKey](const InputEvent& e) {
        ++keyDownCount;
        lastKey = e.key;
    });

    int quitCount = 0;
    input->RegisterCallback(InputEventType::Quit, [&quitCount](const InputEvent&) { ++quitCount; });

    // 无事件时 Update：回调不应被调用
    input->Update();
    TEST_CHECK(keyDownCount == 0);
    TEST_CHECK(quitCount == 0);

    // 推送 KeyDown（A 键，scancode 与 KeyCode::A 一致为 4）
    SDL_Event ev{};
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.type = SDL_EVENT_KEY_DOWN;
    ev.key.scancode = SDL_SCANCODE_A;
    ev.key.windowID = 0;
    TEST_CHECK(SDL_PushEvent(&ev));

    input->Update();
    TEST_CHECK(keyDownCount == 1);
    TEST_CHECK(lastKey == KeyCode::A);
    TEST_CHECK(quitCount == 0);

    // 同一类型多回调：再注册一个 KeyDown
    int keyDownCount2 = 0;
    input->RegisterCallback(InputEventType::KeyDown, [&keyDownCount2](const InputEvent&) { ++keyDownCount2; });
    ev.key.scancode = SDL_SCANCODE_B;
    TEST_CHECK(SDL_PushEvent(&ev));
    input->Update();
    TEST_CHECK(keyDownCount == 2);
    TEST_CHECK(keyDownCount2 == 1);
    TEST_CHECK(lastKey == KeyCode::B);

    // ClearCallbacks(KeyDown)：只清除 KeyDown，Quit 仍保留
    input->ClearCallbacks(InputEventType::KeyDown);
    ev.key.scancode = SDL_SCANCODE_C;
    TEST_CHECK(SDL_PushEvent(&ev));
    input->Update();
    TEST_CHECK(keyDownCount == 2);   // 未再增加
    TEST_CHECK(keyDownCount2 == 1);
    TEST_CHECK(quitCount == 0);

    // 重新注册 Quit 回调并推送 Quit（仅验证派发，不真正退出测试进程）
    input->RegisterCallback(InputEventType::Quit, [&quitCount](const InputEvent&) { ++quitCount; });
    SDL_Event quitEv{};
    quitEv.type = SDL_EVENT_QUIT;
    TEST_CHECK(SDL_PushEvent(&quitEv));
    input->Update();
    TEST_CHECK(quitCount == 1);

    // ClearAllCallbacks 后不再派发
    input->ClearAllCallbacks();
    quitEv.type = SDL_EVENT_QUIT;
    TEST_CHECK(SDL_PushEvent(&quitEv));
    input->Update();
    TEST_CHECK(quitCount == 1);

    // 空回调不崩溃（RegisterCallback 内部已忽略 null）
    input->RegisterCallback(InputEventType::MouseWheel, nullptr);
    input->Update();

    engine.Shutdown();
    return 0;
}
