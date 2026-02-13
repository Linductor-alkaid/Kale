# Gamepad Monitor - 手柄输入监控示例
# 实时在终端输出手柄按键与摇杆值，无手柄时等待接入
# 仅依赖 SDL3，无需 Vulkan

add_executable(gamepad_monitor
    main.cpp
)

target_link_libraries(gamepad_monitor
    PRIVATE
        SDL3::SDL3
)
