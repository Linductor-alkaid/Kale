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

add_executable(test_load_async test_load_async.cpp)
target_link_libraries(test_load_async
    PRIVATE
        kale_resource
        kale_executor
)
add_test(NAME test_load_async COMMAND test_load_async)

add_executable(test_placeholders test_placeholders.cpp)
target_link_libraries(test_placeholders
    PRIVATE
        kale_resource
        kale_device
)
add_test(NAME test_placeholders COMMAND test_placeholders)

add_executable(test_draw_resource_check test_draw_resource_check.cpp)
target_link_libraries(test_draw_resource_check
    PRIVATE
        kale_resource
        kale_scene
        kale_device
)
add_test(NAME test_draw_resource_check COMMAND test_draw_resource_check)

add_executable(test_scene_node_ref test_scene_node_ref.cpp)
target_link_libraries(test_scene_node_ref
    PRIVATE
        kale_scene
)
add_test(NAME test_scene_node_ref COMMAND test_scene_node_ref)

add_executable(test_entity_component_storage test_entity_component_storage.cpp)
target_link_libraries(test_entity_component_storage
    PRIVATE
        kale_scene
)
add_test(NAME test_entity_component_storage COMMAND test_entity_component_storage)

add_executable(test_entity_manager test_entity_manager.cpp)
target_link_libraries(test_entity_manager
    PRIVATE
        kale_scene
)
add_test(NAME test_entity_manager COMMAND test_entity_manager)

add_executable(test_system_base test_system_base.cpp)
target_link_libraries(test_system_base
    PRIVATE
        kale_scene
)
add_test(NAME test_system_base COMMAND test_system_base)

add_executable(test_ecs_executor_integration test_ecs_executor_integration.cpp)
target_link_libraries(test_ecs_executor_integration
    PRIVATE
        kale_scene
        kale_executor
)
add_test(NAME test_ecs_executor_integration COMMAND test_ecs_executor_integration)

add_executable(test_writeback_flow test_writeback_flow.cpp)
target_link_libraries(test_writeback_flow
    PRIVATE
        kale_scene
)
add_test(NAME test_writeback_flow COMMAND test_writeback_flow)
