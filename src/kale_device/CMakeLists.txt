# Kale 设备抽象层 (Device Abstraction Layer)
# RDI、窗口系统、输入管理
# 依赖：Vulkan、SDL3、glm；可选 OpenGL

set(kale_device_sources
    placeholder.cpp
    window_system.cpp
    vulkan_context.cpp
    vulkan_rdi_utils.cpp
    input_manager.cpp
    render_device.cpp
    vulkan_render_device.cpp
)
if(KALE_ENABLE_OPENGL_BACKEND)
    find_package(OpenGL QUIET)
    if(OpenGL_FOUND)
        list(APPEND kale_device_sources opengl_render_device.cpp)
    endif()
endif()
add_library(kale_device STATIC ${kale_device_sources})

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
if(TARGET VulkanMemoryAllocator)
    target_link_libraries(kale_device PRIVATE VulkanMemoryAllocator)
    target_compile_definitions(kale_device PRIVATE KALE_USE_VMA=1)
endif()
if(KALE_ENABLE_OPENGL_BACKEND AND OpenGL_FOUND)
    target_link_libraries(kale_device PUBLIC OpenGL::OpenGL)
    target_compile_definitions(kale_device PRIVATE KALE_HAS_OPENGL_BACKEND=1)
endif()
