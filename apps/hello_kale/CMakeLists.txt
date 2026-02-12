# Hello Kale - 最小示例应用
# 验证引擎初始化与主循环

add_executable(hello_kale
    main.cpp
)

target_link_libraries(hello_kale
    PRIVATE
        kale_engine
)
