# Kale 引擎主入口 (RenderEngine)
# 聚合各层，提供 Application 接口与主循环
# 依赖：所有 kale_* 子模块

add_library(kale_engine STATIC
    placeholder.cpp
    render_engine.cpp
)

target_include_directories(kale_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kale_engine
    PUBLIC
        kale_device
        kale_executor
        kale_resource
        kale_scene
        kale_pipeline
)
