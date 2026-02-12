# Kale 渲染引擎 - 总 CMake 配置
# Vulkan + SDL3 渲染引擎，模块化分层架构
#
# 构建方式：每个子模块独立 CMake，根 CMake 负责项目配置与依赖聚合

cmake_minimum_required(VERSION 3.16)
project(kale
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "Vulkan + SDL3 渲染引擎"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 导出编译命令（IDE、调试用）
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 项目选项
option(KALE_BUILD_APPS "构建示例应用" ON)
option(KALE_ENABLE_VALIDATION "启用 Vulkan Validation Layer（Debug）" ON)
option(KALE_ENABLE_OPENGL_BACKEND "构建 OpenGL 后端" OFF)
option(KALE_ENABLE_TSAN "启用 ThreadSanitizer 检测数据竞争" OFF)

# 依赖路径（可通过 -D 传入）
set(KALE_EXECUTOR_PATH "" CACHE PATH "executor 库路径（add_subdirectory 用）")
set(KALE_USE_VCPKG OFF CACHE BOOL "使用 vcpkg 管理依赖")
set(KALE_USE_SYSTEM_SDL3 OFF CACHE BOOL "使用系统安装的 SDL3")

# 第三方依赖查找（根 CMake 统一配置，子模块通过 target_link_libraries 使用）
if(KALE_USE_VCPKG)
    find_package(glm CONFIG REQUIRED)
    find_package(SDL3 CONFIG REQUIRED)
    find_package(Vulkan REQUIRED)
else()
    # 查找 glm
    find_package(glm QUIET)
    if(NOT glm_FOUND)
        include(FetchContent)
        FetchContent_Declare(glm
            GIT_REPOSITORY https://github.com/g-truc/glm.git
            GIT_TAG 1.0.1
        )
        FetchContent_MakeAvailable(glm)
    endif()

    # 查找 SDL3：优先使用 third_party/SDL，其次系统安装
    if(KALE_USE_SYSTEM_SDL3)
        find_package(SDL3 QUIET)
    endif()

    # 如果未找到系统 SDL3，使用 third_party/SDL
    if(NOT SDL3_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SDL/CMakeLists.txt")
        message(STATUS "Using SDL3 from third_party/SDL")
        set(SDL3_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL3_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory(third_party/SDL third_party/SDL EXCLUDE_FROM_ALL)
    elseif(NOT SDL3_FOUND)
        message(FATAL_ERROR "SDL3 not found. Please install SDL3 or ensure third_party/SDL exists")
    endif()

    find_package(Vulkan QUIET)
endif()

# stb（单头文件库：stb_image 用于 PNG/JPG 加载）
include(FetchContent)
FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb
    GIT_TAG        master
)
FetchContent_Populate(stb)
if(NOT TARGET stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
endif()

# executor 库：通过 -DKALE_EXECUTOR_PATH=/path/to/executor 指定
if(KALE_EXECUTOR_PATH)
    add_subdirectory(${KALE_EXECUTOR_PATH} ${CMAKE_BINARY_DIR}/executor EXCLUDE_FROM_ALL)
else()
    find_package(executor QUIET)
endif()

# 子模块：各层独立 CMake，按依赖顺序添加
add_subdirectory(src/kale_device)
add_subdirectory(src/kale_executor)
add_subdirectory(src/kale_resource)
add_subdirectory(src/kale_scene)
add_subdirectory(src/kale_pipeline)
add_subdirectory(src/kale_engine)

# 示例应用（可选）
if(KALE_BUILD_APPS)
    add_subdirectory(apps)
endif()

# ThreadSanitizer（检测数据竞争，需在支持的环境中运行）
if(KALE_ENABLE_TSAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
    message(STATUS "ThreadSanitizer enabled")
endif()

# 测试（可选）
option(KALE_BUILD_TESTS "构建测试" ON)
if(KALE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Kale ${PROJECT_VERSION} - Build type: ${CMAKE_BUILD_TYPE}")
