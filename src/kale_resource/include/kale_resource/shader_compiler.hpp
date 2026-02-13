/**
 * @file shader_compiler.hpp
 * @brief 着色器编译器：加载 .spv、可选 GLSL→SPIR-V，供 Material/RenderGraph 使用
 *
 * 非 IResourceLoader，独立接口。支持 .vert、.frag、.comp、.spv；
 * .spv 直接加载；.vert/.frag/.comp 可预编译为同路径 .spv（如 foo.vert.spv）或运行时 GLSL 编译（若启用）。
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <kale_device/rdi_types.hpp>
#include <kale_device/render_device.hpp>

namespace kale::resource {

/**
 * @brief 着色器编译器
 *
 * Compile(path, stage, device)：按扩展名加载或编译，返回 ShaderHandle。
 * LoadSPIRV(path)：仅读取 .spv 二进制到内存。
 * CompileGLSLToSPIRV(path, stage)：GLSL→SPIR-V（需 glslang/shaderc，未链接时返回空）。
 * Recompile：与 Compile 相同，热重载时调用（调用方负责销毁旧句柄）。
 */
class ShaderCompiler {
public:
    ShaderCompiler() = default;

    /** 设置资源根路径，ResolvePath 时拼接 */
    void SetBasePath(const std::string& path) { basePath_ = path; }
    const std::string& GetBasePath() const { return basePath_; }

    /**
     * 解析路径：若 basePath 非空则返回 basePath + path（并规范化分隔符），否则返回 path。
     */
    std::string ResolvePath(const std::string& path) const;

    /**
     * 加载 SPIR-V 二进制文件到 outCode。
     * @return 成功返回 true，失败返回 false 并设置 GetLastError()。
     */
    bool LoadSPIRV(const std::string& path, std::vector<std::uint8_t>& outCode);

    /**
     * GLSL 源码编译为 SPIR-V。
     * 若未集成 glslang/shaderc，则尝试加载 path + ".spv"（如 xxx.vert → xxx.vert.spv）。
     * @return SPIR-V 字节序列，失败或不可用时返回空。
     */
    std::vector<std::uint8_t> CompileGLSLToSPIRV(const std::string& path,
                                                  kale_device::ShaderStage stage);

    /**
     * 加载并编译着色器，创建 RDI Shader。
     * .spv → LoadSPIRV + CreateShader；.vert/.frag/.comp → CompileGLSLToSPIRV（或同名校 .spv）+ CreateShader。
     * @param path 资源路径（可经 ResolvePath 解析）
     * @param stage 着色器阶段（.spv 时由调用方指定；.vert/.frag/.comp 可据此推断）
     * @param device 渲染设备，不可为 nullptr
     * @return 有效 ShaderHandle 或无效句柄，失败时 GetLastError() 有原因。
     */
    kale_device::ShaderHandle Compile(const std::string& path,
                                      kale_device::ShaderStage stage,
                                      kale_device::IRenderDevice* device);

    /**
     * 热重载时重新编译（与 Compile 行为一致；调用方需先 DestroyShader 旧句柄）。
     */
    kale_device::ShaderHandle Recompile(const std::string& path,
                                        kale_device::ShaderStage stage,
                                        kale_device::IRenderDevice* device);

    const std::string& GetLastError() const { return lastError_; }

    /** 根据扩展名推断 ShaderStage（.vert→Vertex, .frag→Fragment, .comp→Compute），未知返回 Vertex */
    static kale_device::ShaderStage StageFromExtension(const std::string& path);

    /** 是否支持该扩展名（.vert / .frag / .comp / .spv） */
    static bool SupportsExtension(const std::string& path);

private:
    std::string basePath_;
    std::string lastError_;

    void SetLastError(const std::string& msg) { lastError_ = msg; }
};

}  // namespace kale::resource
