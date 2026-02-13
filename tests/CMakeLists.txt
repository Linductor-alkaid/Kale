# Kale 测试

add_executable(test_task_channel test_task_channel.cpp)

target_link_libraries(test_task_channel
    PRIVATE
        kale_executor
)

add_test(NAME test_task_channel COMMAND test_task_channel)

add_executable(test_executor_future test_executor_future.cpp)

target_link_libraries(test_executor_future
    PRIVATE
        kale_executor
)

add_test(NAME test_executor_future COMMAND test_executor_future)

add_executable(test_resource_loader test_resource_loader.cpp)
target_link_libraries(test_resource_loader
    PRIVATE
        kale_resource
)
add_test(NAME test_resource_loader COMMAND test_resource_loader)
