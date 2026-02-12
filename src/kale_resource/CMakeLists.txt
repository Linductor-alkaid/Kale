# Kale 资源管理层 (Resource Management Layer)
# ResourceManager、ResourceCache、Loaders、Staging Memory
# 依赖：kale_device、kale_executor

add_library(kale_resource STATIC
    placeholder.cpp
    resource_manager.cpp
    # 待实现：texture_loader.cpp, model_loader.cpp
)

target_include_directories(kale_resource
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kale_resource
    PUBLIC
        kale_device
        kale_executor
)
