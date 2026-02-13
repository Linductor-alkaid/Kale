/**
 * @file main.cpp
 * @brief 手柄输入监控示例：实时在终端输出手柄按键与摇杆值
 *
 * 无手柄时等待接入，按 Escape 退出。
 * 仅依赖 SDL3，无需 Vulkan。
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <thread>
#include <unordered_map>

namespace {

constexpr int kPollIntervalMs = 50;   // 轮询间隔
constexpr int kWaitMessageIntervalMs = 2000;  // 无手柄时提示间隔

const char* AxisName(int axis) {
    switch (axis) {
        case SDL_GAMEPAD_AXIS_LEFTX: return "LeftX";
        case SDL_GAMEPAD_AXIS_LEFTY: return "LeftY";
        case SDL_GAMEPAD_AXIS_RIGHTX: return "RightX";
        case SDL_GAMEPAD_AXIS_RIGHTY: return "RightY";
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return "LTrigger";
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return "RTrigger";
        default: return "?";
    }
}

const char* ButtonName(int button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return "South(A)";
        case SDL_GAMEPAD_BUTTON_EAST: return "East(B)";
        case SDL_GAMEPAD_BUTTON_WEST: return "West(X)";
        case SDL_GAMEPAD_BUTTON_NORTH: return "North(Y)";
        case SDL_GAMEPAD_BUTTON_BACK: return "Back";
        case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
        case SDL_GAMEPAD_BUTTON_START: return "Start";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "LStick";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "RStick";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LShoulder";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RShoulder";
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return "DpadUp";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "DpadDown";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "DpadLeft";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "DpadRight";
        default: return "?";
    }
}

}  // namespace

int main() {
    // 允许后台接收手柄输入（隐藏窗口时必需）
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    // SDL3 返回 true=成功 false=失败
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        // 首次失败时尝试 dummy 驱动（无显示/SSH 等环境）
        if (!std::getenv("SDL_VIDEODRIVER")) {
            setenv("SDL_VIDEODRIVER", "dummy", 1);
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
                std::fprintf(stderr, "SDL_Init (dummy) OK, 使用 dummy 视频驱动\n");
            } else {
                const char* err = SDL_GetError();
                std::fprintf(stderr, "SDL_Init failed: %s\n", err && err[0] ? err : "(no message)");
                return 1;
            }
        } else {
            const char* err = SDL_GetError();
            std::fprintf(stderr, "SDL_Init failed: %s\n", err && err[0] ? err : "(no message)");
            return 1;
        }
    }

    // 创建小窗口（部分平台需可见窗口才能接收手柄输入；dummy 驱动下手柄可能无数据）
    SDL_Window* window = SDL_CreateWindow("Gamepad Monitor", 320, 240, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        std::fprintf(stderr, "无显示时尝试: SDL_VIDEODRIVER=dummy ./gamepad_monitor\n");
        SDL_Quit();
        return 1;
    }

    if (std::getenv("SDL_VIDEODRIVER") &&
        std::string(std::getenv("SDL_VIDEODRIVER")) == "dummy") {
        std::fprintf(stderr, "提示: dummy 驱动下手柄输入可能为 0，请在有显示环境下运行\n");
    }

    std::printf("Gamepad Monitor - 按 Escape 退出\n");
    std::printf("等待手柄接入...\n");

    std::unordered_map<SDL_JoystickID, SDL_Gamepad*> openGamepads;
    bool quit = false;
    auto lastWaitMessage = std::chrono::steady_clock::now();

    while (!quit) {
        SDL_PumpEvents();

        // 处理事件（含热插拔、退出）
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    quit = true;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.scancode == SDL_SCANCODE_ESCAPE)
                        quit = true;
                    break;
                case SDL_EVENT_GAMEPAD_REMOVED: {
                    SDL_JoystickID id = event.gdevice.which;
                    auto it = openGamepads.find(id);
                    if (it != openGamepads.end()) {
                        if (it->second) SDL_CloseGamepad(it->second);
                        openGamepads.erase(it);
                        std::printf("\n[手柄已断开] instance_id=%d\n", static_cast<int>(id));
                    }
                    break;
                }
                case SDL_EVENT_GAMEPAD_ADDED:
                    // 在下方轮询时打开
                    break;
                default:
                    break;
            }
        }

        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (!ids) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
            continue;
        }

        if (count == 0) {
            SDL_free(ids);
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastWaitMessage).count();
            if (elapsed >= kWaitMessageIntervalMs) {
                std::printf("等待手柄接入...\n");
                lastWaitMessage = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
            continue;
        }

        // 有手柄：打开未打开的，并轮询状态
        lastWaitMessage = std::chrono::steady_clock::now();
        bool anyOutput = false;

        for (int i = 0; i < count; ++i) {
            SDL_JoystickID instanceId = ids[i];
            SDL_Gamepad* gp = nullptr;
            auto it = openGamepads.find(instanceId);
            if (it != openGamepads.end()) {
                gp = it->second;
            } else {
                gp = SDL_OpenGamepad(instanceId);
                if (gp) {
                    openGamepads[instanceId] = gp;
                    std::printf("\n[手柄已接入] instance_id=%d index=%d\n",
                               static_cast<int>(instanceId), i);
                }
            }
            if (!gp) continue;

            // 轴（摇杆 -32768~32767，扳机 0~32767）
            for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
                Sint16 raw = SDL_GetGamepadAxis(gp, static_cast<SDL_GamepadAxis>(a));
                float norm = (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
                              a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
                                 ? (raw <= 0 ? 0.0f : static_cast<float>(raw) / 32767.0f)
                                 : static_cast<float>(raw) / 32767.0f;
                if (norm != 0.0f) {
                    std::printf("[%d] %s=%.3f ", i, AxisName(a), norm);
                    anyOutput = true;
                }
            }

            // 按钮
            for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
                if (SDL_GetGamepadButton(gp, static_cast<SDL_GamepadButton>(b))) {
                    std::printf("[%d] %s=1 ", i, ButtonName(b));
                    anyOutput = true;
                }
            }
        }
        SDL_free(ids);

        if (anyOutput) std::printf("\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }

    for (auto& kv : openGamepads) {
        if (kv.second) SDL_CloseGamepad(kv.second);
    }
    openGamepads.clear();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("已退出。\n");
    return 0;
}
