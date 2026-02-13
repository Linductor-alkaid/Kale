# Kale 场景管理层 (Scene Management Layer)
# Scene Graph、ECS、Culling、SceneNodeRef
# 依赖：kale_device、kale_executor、kale_resource

add_library(kale_scene STATIC
    placeholder.cpp
    scene_manager.cpp
    frustum.cpp
    static_mesh.cpp
    # 待实现：scene_node.cpp, entity_manager.cpp, culling.cpp
)

target_include_directories(kale_scene
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kale_scene
    PUBLIC
        kale_device
        kale_executor
        kale_resource
)
