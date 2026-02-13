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

add_executable(test_task_data_manager test_task_data_manager.cpp)
target_link_libraries(test_task_data_manager
    PRIVATE
        kale_executor
)
add_test(NAME test_task_data_manager COMMAND test_task_data_manager)

add_executable(test_task_graph test_task_graph.cpp)
target_link_libraries(test_task_graph
    PRIVATE
        kale_executor
)
add_test(NAME test_task_graph COMMAND test_task_graph)

add_executable(test_submit_task_graph test_submit_task_graph.cpp)
target_link_libraries(test_submit_task_graph
    PRIVATE
        kale_executor
)
add_test(NAME test_submit_task_graph COMMAND test_submit_task_graph)

add_executable(test_frame_data_swap_buffer test_frame_data_swap_buffer.cpp)
target_link_libraries(test_frame_data_swap_buffer
    PRIVATE
        kale_executor
)
add_test(NAME test_frame_data_swap_buffer COMMAND test_frame_data_swap_buffer)

add_executable(test_render_task_scheduler test_render_task_scheduler.cpp)
target_link_libraries(test_render_task_scheduler
    PRIVATE
        kale_executor
)
add_test(NAME test_render_task_scheduler COMMAND test_render_task_scheduler)

add_executable(test_resource_loader test_resource_loader.cpp)
target_link_libraries(test_resource_loader
    PRIVATE
        kale_resource
)
add_test(NAME test_resource_loader COMMAND test_resource_loader)
