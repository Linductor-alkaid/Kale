# Kale 测试

add_executable(test_task_channel test_task_channel.cpp)

target_link_libraries(test_task_channel
    PRIVATE
        kale_executor
)

add_test(NAME test_task_channel COMMAND test_task_channel)
