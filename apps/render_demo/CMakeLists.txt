# Render Demo - 完整渲染示例
# 展示：场景图、相机、程序化立方体、Forward Pass、SubmitVisibleToRenderGraph

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
            ${SHADER_OUT_DIR}/cube.vert.spv
            ${SHADER_OUT_DIR}/cube.frag.spv
        COMMAND ${GLSLC} -o ${SHADER_OUT_DIR}/cube.vert.spv ${SHADER_DIR}/cube.vert
        COMMAND ${GLSLC} -o ${SHADER_OUT_DIR}/cube.frag.spv ${SHADER_DIR}/cube.frag
        DEPENDS ${SHADER_DIR}/cube.vert ${SHADER_DIR}/cube.frag
        COMMENT "Compiling cube shaders to SPIR-V"
    )
    add_custom_target(render_demo_shaders ALL DEPENDS
        ${SHADER_OUT_DIR}/cube.vert.spv
        ${SHADER_OUT_DIR}/cube.frag.spv
    )
elseif(GLSLANG_VALIDATOR)
    add_custom_command(
        OUTPUT
            ${SHADER_OUT_DIR}/cube.vert.spv
            ${SHADER_OUT_DIR}/cube.frag.spv
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${SHADER_OUT_DIR}/cube.vert.spv ${SHADER_DIR}/cube.vert
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${SHADER_OUT_DIR}/cube.frag.spv ${SHADER_DIR}/cube.frag
        DEPENDS ${SHADER_DIR}/cube.vert ${SHADER_DIR}/cube.frag
        COMMENT "Compiling cube shaders to SPIR-V (glslangValidator)"
    )
    add_custom_target(render_demo_shaders ALL DEPENDS
        ${SHADER_OUT_DIR}/cube.vert.spv
        ${SHADER_OUT_DIR}/cube.frag.spv
    )
else()
    message(WARNING "glslc/glslangValidator not found. Shaders must be pre-compiled in ${SHADER_OUT_DIR}")
endif()

add_executable(render_demo main.cpp)

target_compile_definitions(render_demo PRIVATE
    RENDER_DEMO_SHADER_DIR="${SHADER_OUT_DIR}"
)

if(TARGET render_demo_shaders)
    add_dependencies(render_demo render_demo_shaders)
endif()

target_link_libraries(render_demo
    PRIVATE
        kale_engine
)
