# Kale 渲染管线层 (Rendering Pipeline Layer)
# Render Graph、Material、Shader、Render Passes
# 依赖：kale_device、kale_executor、kale_resource、kale_scene

add_library(kale_pipeline STATIC
    placeholder.cpp
    material_loader.cpp
    material_pipeline_reload_registry.cpp
    instanced_draw.cpp
    src/shader_manager.cpp
    # 待实现：render_graph.cpp, material.cpp, passes/*
)

target_include_directories(kale_pipeline
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kale_pipeline
    PUBLIC
        kale_device
        kale_executor
        kale_resource
        kale_scene
    PRIVATE
        nlohmann_json::nlohmann_json
)
