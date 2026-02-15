# Kale 渲染管线层 (Rendering Pipeline Layer)
# Render Graph、Material、Shader、Render Passes
# 依赖：kale_device、kale_executor、kale_resource、kale_scene

set(TONE_MAPPING_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(TONE_MAPPING_SPV_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${TONE_MAPPING_SPV_DIR})
find_program(GLSLC glslc)
find_program(GLSLANG_VALIDATOR glslangValidator)
set(BLOOM_SHADER_OUTPUTS
    ${TONE_MAPPING_SPV_DIR}/extract_brightness.vert.spv
    ${TONE_MAPPING_SPV_DIR}/extract_brightness.frag.spv
    ${TONE_MAPPING_SPV_DIR}/blur.vert.spv
    ${TONE_MAPPING_SPV_DIR}/blur.frag.spv
    ${TONE_MAPPING_SPV_DIR}/composite_tone_map.vert.spv
    ${TONE_MAPPING_SPV_DIR}/composite_tone_map.frag.spv
)
if(GLSLC)
    add_custom_command(OUTPUT ${TONE_MAPPING_SPV_DIR}/tone_mapping.vert.spv ${TONE_MAPPING_SPV_DIR}/tone_mapping.frag.spv
        COMMAND ${GLSLC} -o ${TONE_MAPPING_SPV_DIR}/tone_mapping.vert.spv ${TONE_MAPPING_SHADER_DIR}/tone_mapping.vert
        COMMAND ${GLSLC} -o ${TONE_MAPPING_SPV_DIR}/tone_mapping.frag.spv ${TONE_MAPPING_SHADER_DIR}/tone_mapping.frag
        DEPENDS ${TONE_MAPPING_SHADER_DIR}/tone_mapping.vert ${TONE_MAPPING_SHADER_DIR}/tone_mapping.frag
        COMMENT "Compiling tone_mapping shaders to SPIR-V")
    add_custom_command(OUTPUT ${BLOOM_SHADER_OUTPUTS}
        COMMAND ${GLSLC} -o ${TONE_MAPPING_SPV_DIR}/extract_brightness.vert.spv ${TONE_MAPPING_SHADER_DIR}/extract_brightness.vert
        COMMAND ${GLSLC} -o ${TONE_MAPPING_SPV_DIR}/extract_brightness.frag.spv ${TONE_MAPPING_SHADER_DIR}/extract_brightness.frag
        COMMAND ${GLSLC} -o ${TONE_MAPPING_SPV_DIR}/blur.vert.spv ${TONE_MAPPING_SHADER_DIR}/blur.vert
        COMMAND ${GLSLC} -o ${TONE_MAPPING_SPV_DIR}/blur.frag.spv ${TONE_MAPPING_SHADER_DIR}/blur.frag
        COMMAND ${GLSLC} -o ${TONE_MAPPING_SPV_DIR}/composite_tone_map.vert.spv ${TONE_MAPPING_SHADER_DIR}/composite_tone_map.vert
        COMMAND ${GLSLC} -o ${TONE_MAPPING_SPV_DIR}/composite_tone_map.frag.spv ${TONE_MAPPING_SHADER_DIR}/composite_tone_map.frag
        DEPENDS ${TONE_MAPPING_SHADER_DIR}/extract_brightness.vert ${TONE_MAPPING_SHADER_DIR}/extract_brightness.frag
            ${TONE_MAPPING_SHADER_DIR}/blur.vert ${TONE_MAPPING_SHADER_DIR}/blur.frag
            ${TONE_MAPPING_SHADER_DIR}/composite_tone_map.vert ${TONE_MAPPING_SHADER_DIR}/composite_tone_map.frag
        COMMENT "Compiling Bloom shaders to SPIR-V")
    add_custom_target(kale_pipeline_tone_mapping_shaders ALL
        DEPENDS ${TONE_MAPPING_SPV_DIR}/tone_mapping.vert.spv ${TONE_MAPPING_SPV_DIR}/tone_mapping.frag.spv
            ${BLOOM_SHADER_OUTPUTS})
elseif(GLSLANG_VALIDATOR)
    add_custom_command(OUTPUT ${TONE_MAPPING_SPV_DIR}/tone_mapping.vert.spv ${TONE_MAPPING_SPV_DIR}/tone_mapping.frag.spv
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${TONE_MAPPING_SPV_DIR}/tone_mapping.vert.spv ${TONE_MAPPING_SHADER_DIR}/tone_mapping.vert
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${TONE_MAPPING_SPV_DIR}/tone_mapping.frag.spv ${TONE_MAPPING_SHADER_DIR}/tone_mapping.frag
        DEPENDS ${TONE_MAPPING_SHADER_DIR}/tone_mapping.vert ${TONE_MAPPING_SHADER_DIR}/tone_mapping.frag
        COMMENT "Compiling tone_mapping shaders to SPIR-V (glslangValidator)")
    add_custom_command(OUTPUT ${BLOOM_SHADER_OUTPUTS}
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${TONE_MAPPING_SPV_DIR}/extract_brightness.vert.spv ${TONE_MAPPING_SHADER_DIR}/extract_brightness.vert
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${TONE_MAPPING_SPV_DIR}/extract_brightness.frag.spv ${TONE_MAPPING_SHADER_DIR}/extract_brightness.frag
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${TONE_MAPPING_SPV_DIR}/blur.vert.spv ${TONE_MAPPING_SHADER_DIR}/blur.vert
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${TONE_MAPPING_SPV_DIR}/blur.frag.spv ${TONE_MAPPING_SHADER_DIR}/blur.frag
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${TONE_MAPPING_SPV_DIR}/composite_tone_map.vert.spv ${TONE_MAPPING_SHADER_DIR}/composite_tone_map.vert
        COMMAND ${GLSLANG_VALIDATOR} -V -o ${TONE_MAPPING_SPV_DIR}/composite_tone_map.frag.spv ${TONE_MAPPING_SHADER_DIR}/composite_tone_map.frag
        DEPENDS ${TONE_MAPPING_SHADER_DIR}/extract_brightness.vert ${TONE_MAPPING_SHADER_DIR}/extract_brightness.frag
            ${TONE_MAPPING_SHADER_DIR}/blur.vert ${TONE_MAPPING_SHADER_DIR}/blur.frag
            ${TONE_MAPPING_SHADER_DIR}/composite_tone_map.vert ${TONE_MAPPING_SHADER_DIR}/composite_tone_map.frag
        COMMENT "Compiling Bloom shaders to SPIR-V (glslangValidator)")
    add_custom_target(kale_pipeline_tone_mapping_shaders ALL
        DEPENDS ${TONE_MAPPING_SPV_DIR}/tone_mapping.vert.spv ${TONE_MAPPING_SPV_DIR}/tone_mapping.frag.spv
            ${BLOOM_SHADER_OUTPUTS})
else()
    message(WARNING "glslc/glslangValidator not found. Tone mapping shaders will not be compiled; SetToneMappingShaderDirectory will need pre-built .spv")
endif()

add_library(kale_pipeline STATIC
    placeholder.cpp
    material_loader.cpp
    material_pipeline_reload_registry.cpp
    instanced_draw.cpp
    src/shader_manager.cpp
    post_process_pass.cpp
)
if(TARGET kale_pipeline_tone_mapping_shaders)
    add_dependencies(kale_pipeline kale_pipeline_tone_mapping_shaders)
endif()

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
