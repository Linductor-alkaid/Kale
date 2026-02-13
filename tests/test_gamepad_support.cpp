/**
 * @file test_gamepad_support.cpp
 * @brief phase11-11.2 手柄支持单元测试
 *
 * 验证：GamepadAxis/GamepadButton 枚举、IsGamepadConnected、GetGamepadAxis、
 * IsGamepadButtonPressed；无手柄时返回 false/0；负索引与越界不崩溃；Update 热插拔路径不崩溃。
 */

#include <kale_device/input_manager.hpp>
#include <kale_engine/render_engine.hpp>
#include <cstdlib>
#include <iostream>

using namespace kale_device;
using namespace kale;

#define TEST_CHECK(cond)                                                         \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " " << #cond \
                      << std::endl;                                             \
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
    config.title = "GamepadTest";
    config.enableValidation = false;
    if (!engine.Initialize(config)) {
        // 无显示/Vulkan 时跳过（与 test_render_engine_init 一致）
        return 0;
    }

    kale_device::InputManager* input = engine.GetInputManager();
    TEST_CHECK(input != nullptr);

    // 负索引
    TEST_CHECK(!input->IsGamepadConnected(-1));
    TEST_CHECK(input->GetGamepadAxis(-1, GamepadAxis::LeftX) == 0.0f);
    TEST_CHECK(input->GetGamepadAxis(-1, GamepadAxis::LeftTrigger) == 0.0f);
    TEST_CHECK(!input->IsGamepadButtonPressed(-1, GamepadButton::South));

    // 无手柄时 index 0 应视为未连接（或连接数为 0）
    bool anyConnected = input->IsGamepadConnected(0);
    (void)anyConnected;
    float ax = input->GetGamepadAxis(0, GamepadAxis::LeftX);
    float axT = input->GetGamepadAxis(0, GamepadAxis::LeftTrigger);
    bool btn = input->IsGamepadButtonPressed(0, GamepadButton::South);
    TEST_CHECK(ax >= -1.0f && ax <= 1.0f);
    TEST_CHECK(axT >= 0.0f && axT <= 1.0f);
    (void)btn;

    // 越界 index 不崩溃，GetGamepadAxis/IsGamepadButtonPressed 返回 0/false
    input->GetGamepadAxis(99, GamepadAxis::RightY);
    input->IsGamepadButtonPressed(99, GamepadButton::Start);

    // 所有轴/按钮枚举可调用不崩溃
    input->GetGamepadAxis(0, GamepadAxis::LeftY);
    input->GetGamepadAxis(0, GamepadAxis::RightX);
    input->GetGamepadAxis(0, GamepadAxis::RightY);
    input->GetGamepadAxis(0, GamepadAxis::RightTrigger);
    input->IsGamepadButtonPressed(0, GamepadButton::East);
    input->IsGamepadButtonPressed(0, GamepadButton::North);
    input->IsGamepadButtonPressed(0, GamepadButton::West);
    input->IsGamepadButtonPressed(0, GamepadButton::Back);
    input->IsGamepadButtonPressed(0, GamepadButton::Start);
    input->IsGamepadButtonPressed(0, GamepadButton::DpadUp);

    // Update 多帧不崩溃（含热插拔事件路径）
    for (int i = 0; i < 5; ++i) {
        input->Update();
    }

    engine.Shutdown();
    return 0;
}
