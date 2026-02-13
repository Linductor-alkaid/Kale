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
    # 查找 glm：优先 third_party/glm，其次 find_package，最后 FetchContent
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glm/CMakeLists.txt")
        message(STATUS "Using glm from third_party/glm")
        add_subdirectory(third_party/glm third_party/glm EXCLUDE_FROM_ALL)
    else()
        find_package(glm QUIET)
        if(NOT glm_FOUND)
            include(FetchContent)
            FetchContent_Declare(glm
                GIT_REPOSITORY https://github.com/g-truc/glm.git
                GIT_TAG 1.0.1
            )
            FetchContent_MakeAvailable(glm)
        endif()
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
# 优先使用 third_party/stb，否则 FetchContent；可手动拉取：git clone https://github.com/nothings/stb third_party/stb
set(STB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
if(EXISTS "${STB_SOURCE_DIR}/stb_image.h")
    message(STATUS "Using stb from third_party/stb")
else()
    include(FetchContent)
    FetchContent_Declare(stb
        GIT_REPOSITORY https://github.com/nothings/stb
        GIT_TAG        master
    )
    FetchContent_Populate(stb)
    set(STB_SOURCE_DIR ${stb_SOURCE_DIR})
endif()
if(NOT TARGET stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${STB_SOURCE_DIR})
endif()

# tinygltf（glTF 2.0 解析，ModelLoader 使用）
# 优先 third_party/tinygltf，否则 FetchContent；scripts 可拉取到 third_party
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinygltf/CMakeLists.txt")
    message(STATUS "Using tinygltf from third_party/tinygltf")
    set(TINYGLTF_HEADER_ONLY OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/tinygltf third_party/tinygltf EXCLUDE_FROM_ALL)
    # 避免 tinygltf 内嵌 stb 与 kale_resource/texture_loader 的 stb 重复定义；ModelLoader 通过 SetImageLoader 使用 kale_resource 的 stb
    target_compile_definitions(tinygltf PRIVATE TINYGLTF_NO_STB_IMAGE TINYGLTF_NO_STB_IMAGE_WRITE)
else()
    include(FetchContent)
    FetchContent_Declare(tinygltf
        GIT_REPOSITORY https://github.com/syoyo/tinygltf
        GIT_TAG        v2.8.13
    )
    FetchContent_MakeAvailable(tinygltf)
endif()

# nlohmann/json（头文件库，MaterialLoader JSON 解析）
# 优先 third_party/json，否则 FetchContent；scripts 可拉取到 third_party
set(JSON_Install OFF CACHE BOOL "" FORCE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json/CMakeLists.txt")
    message(STATUS "Using nlohmann/json from third_party/json")
    add_subdirectory(third_party/json third_party/json EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json
        GIT_TAG        v3.11.3
    )
    FetchContent_MakeAvailable(json)
endif()

# executor 库：优先 find_package（deb/系统安装），其次 KALE_EXECUTOR_PATH 源码
# 参考 docs/executor/PACKAGE_DEB.md：deb 安装后 find_package(executor) 即可
find_package(executor QUIET)
if(NOT executor_FOUND AND KALE_EXECUTOR_PATH)
    message(STATUS "Using executor from source: ${KALE_EXECUTOR_PATH}")
    add_subdirectory(${KALE_EXECUTOR_PATH} ${CMAKE_BINARY_DIR}/executor EXCLUDE_FROM_ALL)
endif()
if(NOT TARGET executor::executor)
    message(WARNING "executor 未找到。安装 deb 包后可直接 cmake 构建；或设置 -DKALE_EXECUTOR_PATH=path/to/executor")
endif()

# Vulkan Memory Allocator（VMA）：Vulkan 设备层 GPU 内存管理
# 仅当使用 Vulkan 时集成；kale_device 在链接 Vulkan 时链接 VMA
if(TARGET Vulkan::Vulkan)
  set(VMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(VMA_BUILD_UTILS OFF CACHE BOOL "" FORCE)
  set(VMA_INSTALL OFF CACHE BOOL "" FORCE)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator/CMakeLists.txt")
    message(STATUS "Using VMA from third_party/VulkanMemoryAllocator")
    add_subdirectory(third_party/VulkanMemoryAllocator third_party/VulkanMemoryAllocator EXCLUDE_FROM_ALL)
  else()
    include(FetchContent)
    FetchContent_Declare(VulkanMemoryAllocator
      GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
      GIT_TAG        v3.0.1
    )
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
  endif()
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
