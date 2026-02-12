# Kale 设备抽象层 (Device Abstraction Layer)
# RDI、窗口系统、输入管理
# 依赖：Vulkan、SDL3、glm

add_library(kale_device STATIC
    placeholder.cpp
    window_system.cpp
    vulkan_context.cpp
    input_manager.cpp
    # 待实现时添加：device.cpp, rdi.cpp
)

target_include_directories(kale_device
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(kale_device
    PUBLIC
        glm::glm
)

# 可选依赖：需在根 CMake 或环境中配置
if(TARGET SDL3::SDL3)
    target_link_libraries(kale_device PUBLIC SDL3::SDL3)
endif()
if(TARGET Vulkan::Vulkan)
    target_link_libraries(kale_device PUBLIC Vulkan::Vulkan)
endif()
