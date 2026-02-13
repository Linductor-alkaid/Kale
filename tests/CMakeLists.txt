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

add_executable(test_render_graph_parallel_record_integration test_render_graph_parallel_record_integration.cpp)
target_link_libraries(test_render_graph_parallel_record_integration
    PRIVATE
        kale_executor
)
add_test(NAME test_render_graph_parallel_record_integration COMMAND test_render_graph_parallel_record_integration)

add_executable(test_resource_loader test_resource_loader.cpp)
target_link_libraries(test_resource_loader
    PRIVATE
        kale_resource
)
add_test(NAME test_resource_loader COMMAND test_resource_loader)

add_executable(test_bounding_box test_bounding_box.cpp)
target_link_libraries(test_bounding_box
    PRIVATE
        kale_resource
)
add_test(NAME test_bounding_box COMMAND test_bounding_box)

add_executable(test_frustum test_frustum.cpp)
target_link_libraries(test_frustum
    PRIVATE
        kale_scene
)
add_test(NAME test_frustum COMMAND test_frustum)

add_executable(test_renderable test_renderable.cpp)
target_link_libraries(test_renderable
    PRIVATE
        kale_scene
        kale_resource
        kale_device
        glm::glm
)
add_test(NAME test_renderable COMMAND test_renderable)

add_executable(test_load_async test_load_async.cpp)
target_link_libraries(test_load_async
    PRIVATE
        kale_resource
        kale_executor
)
add_test(NAME test_load_async COMMAND test_load_async)

add_executable(test_preload_batch test_preload_batch.cpp)
target_link_libraries(test_preload_batch
    PRIVATE
        kale_resource
        kale_executor
)
add_test(NAME test_preload_batch COMMAND test_preload_batch)

add_executable(test_placeholders test_placeholders.cpp)
target_link_libraries(test_placeholders
    PRIVATE
        kale_resource
        kale_device
)
add_test(NAME test_placeholders COMMAND test_placeholders)

add_executable(test_staging_memory_manager test_staging_memory_manager.cpp)
target_link_libraries(test_staging_memory_manager
    PRIVATE
        kale_resource
        kale_device
)
add_test(NAME test_staging_memory_manager COMMAND test_staging_memory_manager)

add_executable(test_texture_loader_staging test_texture_loader_staging.cpp)
target_link_libraries(test_texture_loader_staging
    PRIVATE
        kale_resource
        kale_device
)
# 将 fixtures 复制到 build/tests，便于直接运行可执行文件时找到
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
add_test(NAME test_texture_loader_staging COMMAND test_texture_loader_staging)
set_tests_properties(test_texture_loader_staging PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests)

add_executable(test_compressed_texture_ktx test_compressed_texture_ktx.cpp)
target_link_libraries(test_compressed_texture_ktx
    PRIVATE
        kale_resource
        kale_device
)
add_test(NAME test_compressed_texture_ktx COMMAND test_compressed_texture_ktx)
set_tests_properties(test_compressed_texture_ktx PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tests)

add_executable(test_draw_resource_check test_draw_resource_check.cpp)
target_link_libraries(test_draw_resource_check
    PRIVATE
        kale_resource
        kale_scene
        kale_device
)
add_test(NAME test_draw_resource_check COMMAND test_draw_resource_check)

add_executable(test_refcount_deferred_release test_refcount_deferred_release.cpp)
target_link_libraries(test_refcount_deferred_release
    PRIVATE
        kale_resource
)
add_test(NAME test_refcount_deferred_release COMMAND test_refcount_deferred_release)

add_executable(test_file_change_detection test_file_change_detection.cpp)
target_link_libraries(test_file_change_detection
    PRIVATE
        kale_resource
)
add_test(NAME test_file_change_detection COMMAND test_file_change_detection)

add_executable(test_scene_node_ref test_scene_node_ref.cpp)
target_link_libraries(test_scene_node_ref
    PRIVATE
        kale_scene
)
add_test(NAME test_scene_node_ref COMMAND test_scene_node_ref)

add_executable(test_camera_node test_camera_node.cpp)
target_link_libraries(test_camera_node
    PRIVATE
        kale_scene
)
add_test(NAME test_camera_node COMMAND test_camera_node)

add_executable(test_cull_scene_multi_camera test_cull_scene_multi_camera.cpp)
target_link_libraries(test_cull_scene_multi_camera
    PRIVATE
        kale_scene
)
add_test(NAME test_cull_scene_multi_camera COMMAND test_cull_scene_multi_camera)

add_executable(test_cull_scene_single_camera test_cull_scene_single_camera.cpp)
target_link_libraries(test_cull_scene_single_camera
  kale_scene kale_resource kale_device glm::glm
)
add_test(NAME test_cull_scene_single_camera COMMAND test_cull_scene_single_camera)

add_executable(test_lod_manager test_lod_manager.cpp)
target_link_libraries(test_lod_manager
    PRIVATE
        kale_scene
        kale_resource
        kale_device
        glm::glm
        kale_executor
)
# kale_scene/static_mesh 会引用 ResourceManager::LoadAsync -> executor，需显式链接
if(TARGET executor::executor)
    target_link_libraries(test_lod_manager PRIVATE executor::executor)
endif()
add_test(NAME test_lod_manager COMMAND test_lod_manager)

add_executable(test_occlusion_culling test_occlusion_culling.cpp)
target_link_libraries(test_occlusion_culling
  PRIVATE
    kale_scene
    kale_resource
    kale_device
    glm::glm
)
add_test(NAME test_occlusion_culling COMMAND test_occlusion_culling)

add_executable(test_scene_node_factories test_scene_node_factories.cpp)
target_link_libraries(test_scene_node_factories
    PRIVATE
        kale_scene
        kale_resource
)
add_test(NAME test_scene_node_factories COMMAND test_scene_node_factories)

add_executable(test_submit_visible test_submit_visible.cpp)
target_link_libraries(test_submit_visible
    PRIVATE
        kale_pipeline
        kale_scene
        kale_resource
        kale_device
)
add_test(NAME test_submit_visible COMMAND test_submit_visible)

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

add_executable(test_ecs_parallel_system_integration test_ecs_parallel_system_integration.cpp)
target_link_libraries(test_ecs_parallel_system_integration
    PRIVATE
        kale_scene
        kale_executor
)
add_test(NAME test_ecs_parallel_system_integration COMMAND test_ecs_parallel_system_integration)

add_executable(test_writeback_flow test_writeback_flow.cpp)
target_link_libraries(test_writeback_flow
    PRIVATE
        kale_scene
)
add_test(NAME test_writeback_flow COMMAND test_writeback_flow)

add_executable(test_scene_switch test_scene_switch.cpp)
target_link_libraries(test_scene_switch
    PRIVATE
        kale_scene
)
add_test(NAME test_scene_switch COMMAND test_scene_switch)

add_executable(test_material_base test_material_base.cpp)
target_link_libraries(test_material_base
    PRIVATE
        kale_pipeline
        kale_resource
        kale_device
)
add_test(NAME test_material_base COMMAND test_material_base)

add_executable(test_material_descriptor_set test_material_descriptor_set.cpp)
target_link_libraries(test_material_descriptor_set
    PRIVATE
        kale_pipeline
        kale_resource
        kale_device
)
add_test(NAME test_material_descriptor_set COMMAND test_material_descriptor_set)

add_executable(test_instance_descriptor_set test_instance_descriptor_set.cpp)
target_link_libraries(test_instance_descriptor_set
    PRIVATE
        kale_pipeline
        kale_device
)
add_test(NAME test_instance_descriptor_set COMMAND test_instance_descriptor_set)

add_executable(test_release_frame_resources test_release_frame_resources.cpp)
target_link_libraries(test_release_frame_resources
    PRIVATE
        kale_pipeline
        kale_scene
        kale_device
        kale_resource
)
add_test(NAME test_release_frame_resources COMMAND test_release_frame_resources)

add_executable(test_descriptor_set_pool test_descriptor_set_pool.cpp)
target_link_libraries(test_descriptor_set_pool
    PRIVATE
        kale_device
)
add_test(NAME test_descriptor_set_pool COMMAND test_descriptor_set_pool)

add_executable(test_pbr_material test_pbr_material.cpp)
target_link_libraries(test_pbr_material
    PRIVATE
        kale_pipeline
        kale_resource
)
add_test(NAME test_pbr_material COMMAND test_pbr_material)

add_executable(test_draw_material_binding test_draw_material_binding.cpp)
target_link_libraries(test_draw_material_binding
    PRIVATE
        kale_pipeline
        kale_scene
        kale_resource
        kale_device
)
add_test(NAME test_draw_material_binding COMMAND test_draw_material_binding)

add_executable(test_instanced_draw test_instanced_draw.cpp)
target_link_libraries(test_instanced_draw
    PRIVATE
        kale_pipeline
        kale_resource
        kale_device
)
add_test(NAME test_instanced_draw COMMAND test_instanced_draw)

add_executable(test_material_loader test_material_loader.cpp)
target_link_libraries(test_material_loader
    PRIVATE
        kale_pipeline
        kale_resource
)
add_test(NAME test_material_loader COMMAND test_material_loader)

add_executable(test_shader_compiler test_shader_compiler.cpp)
target_link_libraries(test_shader_compiler
    PRIVATE
        kale_resource
        kale_device
)
add_test(NAME test_shader_compiler COMMAND test_shader_compiler)

add_executable(test_shader_manager test_shader_manager.cpp)
target_link_libraries(test_shader_manager
    PRIVATE
        kale_pipeline
        kale_resource
        kale_device
)
add_test(NAME test_shader_manager COMMAND test_shader_manager)

add_executable(test_shader_hot_reload test_shader_hot_reload.cpp)
target_link_libraries(test_shader_hot_reload
    PRIVATE
        kale_pipeline
        kale_resource
        kale_device
)
add_test(NAME test_shader_hot_reload COMMAND test_shader_hot_reload)

add_executable(test_render_engine_init test_render_engine_init.cpp)
target_link_libraries(test_render_engine_init
    PRIVATE
        kale_engine
)
add_test(NAME test_render_engine_init COMMAND test_render_engine_init)
set_tests_properties(test_render_engine_init PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(test_render_engine_run test_render_engine_run.cpp)
target_link_libraries(test_render_engine_run
    PRIVATE
        kale_engine
)
add_test(NAME test_render_engine_run COMMAND test_render_engine_run)
set_tests_properties(test_render_engine_run PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(test_window_resize_swapchain test_window_resize_swapchain.cpp)
target_link_libraries(test_window_resize_swapchain
    PRIVATE
        kale_engine
        kale_device
)
add_test(NAME test_window_resize_swapchain COMMAND test_window_resize_swapchain)
set_tests_properties(test_window_resize_swapchain PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(test_gamepad_support test_gamepad_support.cpp)
target_link_libraries(test_gamepad_support
    PRIVATE
        kale_engine
        kale_device
)
add_test(NAME test_gamepad_support COMMAND test_gamepad_support)

add_executable(test_action_mapping test_action_mapping.cpp)
target_link_libraries(test_action_mapping
    PRIVATE
        kale_engine
        kale_device
)
add_test(NAME test_action_mapping COMMAND test_action_mapping)
set_tests_properties(test_action_mapping PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(test_input_event_callback test_input_event_callback.cpp)
target_link_libraries(test_input_event_callback
    PRIVATE
        kale_engine
        kale_device
)
add_test(NAME test_input_event_callback COMMAND test_input_event_callback)
set_tests_properties(test_input_event_callback PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(test_input_double_buffering test_input_double_buffering.cpp)
target_link_libraries(test_input_double_buffering
    PRIVATE
        kale_engine
        kale_device
)
add_test(NAME test_input_double_buffering COMMAND test_input_double_buffering)
set_tests_properties(test_input_double_buffering PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(test_vulkan_multithread_command_pool test_vulkan_multithread_command_pool.cpp)
target_link_libraries(test_vulkan_multithread_command_pool
    PRIVATE
        kale_device
)
add_test(NAME test_vulkan_multithread_command_pool COMMAND test_vulkan_multithread_command_pool)
set_tests_properties(test_vulkan_multithread_command_pool PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(test_vulkan_multithread_constraints test_vulkan_multithread_constraints.cpp)
target_link_libraries(test_vulkan_multithread_constraints
    PRIVATE
        kale_device
)
add_test(NAME test_vulkan_multithread_constraints COMMAND test_vulkan_multithread_constraints)
set_tests_properties(test_vulkan_multithread_constraints PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(test_device_capabilities test_device_capabilities.cpp)
target_link_libraries(test_device_capabilities
    PRIVATE
        kale_device
)
add_test(NAME test_device_capabilities COMMAND test_device_capabilities)
set_tests_properties(test_device_capabilities PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(test_opengl_backend test_opengl_backend.cpp)
target_link_libraries(test_opengl_backend
    PRIVATE
        kale_device
)
if(TARGET SDL3::SDL3)
    target_link_libraries(test_opengl_backend PRIVATE SDL3::SDL3)
endif()
add_test(NAME test_opengl_backend COMMAND test_opengl_backend)
set_tests_properties(test_opengl_backend PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")

add_executable(test_shadow_pass test_shadow_pass.cpp)
target_link_libraries(test_shadow_pass
    PRIVATE
        kale_pipeline
        kale_device
        kale_scene
)
add_test(NAME test_shadow_pass COMMAND test_shadow_pass)

add_executable(test_multi_shadow_passes test_multi_shadow_passes.cpp)
target_link_libraries(test_multi_shadow_passes
    PRIVATE
        kale_pipeline
        kale_device
        kale_scene
)
add_test(NAME test_multi_shadow_passes COMMAND test_multi_shadow_passes)

add_executable(test_gbuffer_pass test_gbuffer_pass.cpp)
target_link_libraries(test_gbuffer_pass
    PRIVATE
        kale_pipeline
        kale_device
        kale_scene
)
add_test(NAME test_gbuffer_pass COMMAND test_gbuffer_pass)

add_executable(test_lighting_pass test_lighting_pass.cpp)
target_link_libraries(test_lighting_pass
    PRIVATE
        kale_pipeline
        kale_device
        kale_scene
)
add_test(NAME test_lighting_pass COMMAND test_lighting_pass)

add_executable(test_post_process_pass test_post_process_pass.cpp)
target_link_libraries(test_post_process_pass
    PRIVATE
        kale_pipeline
        kale_device
        kale_scene
)
add_test(NAME test_post_process_pass COMMAND test_post_process_pass)

add_executable(test_output_to_swapchain_pass test_output_to_swapchain_pass.cpp)
target_link_libraries(test_output_to_swapchain_pass
    PRIVATE
        kale_pipeline
        kale_device
        kale_scene
)
add_test(NAME test_output_to_swapchain_pass COMMAND test_output_to_swapchain_pass)

add_executable(test_multi_viewport_output_target test_multi_viewport_output_target.cpp)
target_link_libraries(test_multi_viewport_output_target
    PRIVATE
        kale_pipeline
        kale_device
)
add_test(NAME test_multi_viewport_output_target COMMAND test_multi_viewport_output_target)

add_executable(test_setup_render_graph test_setup_render_graph.cpp)
target_link_libraries(test_setup_render_graph
    PRIVATE
        kale_pipeline
        kale_device
        kale_scene
)
add_test(NAME test_setup_render_graph COMMAND test_setup_render_graph)

add_executable(test_transparent_pass test_transparent_pass.cpp)
target_link_libraries(test_transparent_pass
    PRIVATE
        kale_pipeline
        kale_device
        kale_scene
        glm::glm
)
add_test(NAME test_transparent_pass COMMAND test_transparent_pass)

add_executable(test_topological_groups test_topological_groups.cpp)
target_link_libraries(test_topological_groups
    PRIVATE
        kale_pipeline
        kale_device
        kale_scene
)
add_test(NAME test_topological_groups COMMAND test_topological_groups)

add_executable(test_frame_pipeline test_frame_pipeline.cpp)
target_link_libraries(test_frame_pipeline
    PRIVATE
        kale_pipeline
        kale_device
        kale_scene
)
add_test(NAME test_frame_pipeline COMMAND test_frame_pipeline)

add_executable(test_multithread_record_passes test_multithread_record_passes.cpp)
target_link_libraries(test_multithread_record_passes
    PRIVATE
        kale_pipeline
        kale_device
        kale_executor
)
add_test(NAME test_multithread_record_passes COMMAND test_multithread_record_passes)

add_executable(test_full_model_loader test_full_model_loader.cpp)
target_link_libraries(test_full_model_loader
    PRIVATE
        kale_resource
        kale_device
)
add_test(NAME test_full_model_loader COMMAND test_full_model_loader)

add_executable(test_model_loader_staging test_model_loader_staging.cpp)
target_link_libraries(test_model_loader_staging
    PRIVATE
        kale_resource
        kale_device
)
add_test(NAME test_model_loader_staging COMMAND test_model_loader_staging)

set_tests_properties(test_gamepad_support PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
