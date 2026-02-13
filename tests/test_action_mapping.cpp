/**
 * @file test_action_mapping.cpp
 * @brief phase11-11.3 Action Mapping 单元测试
 *
 * 验证：InputBinding/GamepadBinding、AddActionBinding、ClearActionBindings、
 * IsActionTriggered、GetActionValue；便捷构造 Keyboard/Mouse/GamepadButton/GamepadAxis；
 * 同一 action 多绑定；未知 action 返回 false/0。
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
    config.title = "ActionMappingTest";
    config.enableValidation = false;
    if (!engine.Initialize(config)) {
        return 0;
    }

    InputManager* input = engine.GetInputManager();
    TEST_CHECK(input != nullptr);

    // 未知 action：IsActionTriggered false，GetActionValue 0
    TEST_CHECK(!input->IsActionTriggered("Nonexistent"));
    TEST_CHECK(input->GetActionValue("Nonexistent") == 0.0f);

    // 同一 action 多绑定（W 与 Space）
    input->AddActionBinding("Jump", Keyboard(KeyCode::Space));
    input->AddActionBinding("Jump", Keyboard(KeyCode::W));
    TEST_CHECK(!input->IsActionTriggered("Jump"));  // 未按键
    TEST_CHECK(input->GetActionValue("Jump") == 0.0f);

    // ClearActionBindings 只清除该 action
    input->AddActionBinding("Shoot", Mouse(MouseButton::Left));
    input->ClearActionBindings("Jump");
    TEST_CHECK(!input->IsActionTriggered("Jump"));
    TEST_CHECK(input->GetActionValue("Jump") == 0.0f);
    TEST_CHECK(!input->IsActionTriggered("Shoot"));
    TEST_CHECK(input->GetActionValue("Shoot") == 0.0f);

    // 手柄轴绑定：GetActionValue 不崩溃，且落在 [-1,1]
    input->AddActionBinding("MoveX", GamepadAxisBinding(0, GamepadAxis::LeftX));
    float moveX = input->GetActionValue("MoveX");
    TEST_CHECK(moveX >= -1.0f && moveX <= 1.0f);

    // 手柄按钮绑定：不崩溃
    input->AddActionBinding("Fire", GamepadButtonBinding(0, GamepadButton::South));
    (void)input->IsActionTriggered("Fire");
    (void)input->GetActionValue("Fire");

    // 再次为同一 action 添加多绑定
    input->AddActionBinding("MoveX", GamepadAxisBinding(0, GamepadAxis::LeftY));
    (void)input->GetActionValue("MoveX");

    // ClearActionBindings 后 GetActionValue 为 0
    input->ClearActionBindings("MoveX");
    TEST_CHECK(input->GetActionValue("MoveX") == 0.0f);
    input->ClearActionBindings("Shoot");
    input->ClearActionBindings("Fire");

    // Update 多帧后 action 查询不崩溃
    for (int i = 0; i < 3; ++i) {
        input->Update();
        (void)input->IsActionTriggered("Nonexistent");
        (void)input->GetActionValue("Nonexistent");
    }

    engine.Shutdown();
    return 0;
}
