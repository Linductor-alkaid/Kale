# Hello Kale - 最小示例应用
# 验证引擎初始化与主循环、三角形渲染（phase1-1.3）

# 编译 GLSL 着色器为 SPIR-V（glslc 或 glslangValidator 二选一）
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})
find_program(GLSLC glslc PATHS "$ENV{VULKAN_SDK}/bin" "$ENV{VK_SDK_PATH}/bin" NO_DEFAULT_PATH)
if(NOT GLSLC)
    find_program(GLSLC glslc)
endif()
find_program(GLSLANG_VALIDATOR glslangValidator)
if(GLSLC)
    add_custom_command(
        OUTPUT
            ${SHADER_OUT_DIR}/triangle.vert.spv
            ${SHADER_OUT_DIR}/triangle.frag.spv
        COMMAND ${GLSLC} -o ${SHADER_OUT_DIR}/triangle.vert.spv ${SHADER_DIR}/triangle.vert
        COMMAND ${GLSLC} -o ${SHADER_OUT_DIR}/triangle.frag.spv ${SHADER_DIR}/triangle.frag
        DEPENDS ${SHADER_DIR}/triangle.vert ${SHADER_DIR}/triangle.frag
        COMMENT "Compiling GLSL to SPIR-V (glslc)"
    )
    add_custom_target(hello_kale_shaders ALL DEPENDS
        ${SHADER_OUT_DIR}/triangle.vert.spv
        ${SHADER_OUT_DIR}/triangle.frag.spv
    )
elseif(GLSLANG_VALIDATOR)
    add_custom_command(
        OUTPUT
            ${SHADER_OUT_DIR}/triangle.vert.spv
            ${SHADER_OUT_DIR}/triangle.frag.spv
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${SHADER_OUT_DIR}/triangle.vert.spv ${SHADER_DIR}/triangle.vert
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${SHADER_OUT_DIR}/triangle.frag.spv ${SHADER_DIR}/triangle.frag
        DEPENDS ${SHADER_DIR}/triangle.vert ${SHADER_DIR}/triangle.frag
        COMMENT "Compiling GLSL to SPIR-V (glslangValidator)"
    )
    add_custom_target(hello_kale_shaders ALL DEPENDS
        ${SHADER_OUT_DIR}/triangle.vert.spv
        ${SHADER_OUT_DIR}/triangle.frag.spv
    )
else()
    message(WARNING "glslc/glslangValidator not found. Install Vulkan SDK (glslc) or glslang-tools (glslangValidator). Shaders must be in runtime path.")
endif()

add_executable(hello_kale
    main.cpp
)

if(TARGET hello_kale_shaders)
    add_dependencies(hello_kale hello_kale_shaders)
endif()

target_link_libraries(hello_kale
    PRIVATE
        kale_engine
)
