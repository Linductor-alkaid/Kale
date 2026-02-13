/**
 * @file shader_compiler.cpp
 * @brief ShaderCompiler 实现：LoadSPIRV、Compile、扩展名推断与 .spv 约定
 */

#include <kale_resource/shader_compiler.hpp>

#include <fstream>
#include <algorithm>
#include <cctype>

namespace kale::resource {

namespace {

std::string NormalizePath(const std::string& a, const std::string& b) {
    std::string base = a;
    while (!base.empty() && (base.back() == '/' || base.back() == '\\'))
        base.pop_back();
    std::string p = b;
    while (!p.empty() && (p.front() == '/' || p.front() == '\\'))
        p.erase(0, 1);
    if (base.empty()) return p;
    if (p.empty()) return base;
    return base + "/" + p;
}

std::string GetExtension(const std::string& path) {
    auto pos = path.find_last_of("./\\");
    if (pos == std::string::npos || path[pos] != '.') return {};
    std::string ext;
    for (std::size_t i = pos + 1; i < path.size(); ++i)
        ext += static_cast<char>(std::tolower(static_cast<unsigned char>(path[i])));
    return ext;
}

}  // namespace

std::string ShaderCompiler::ResolvePath(const std::string& path) const {
    if (basePath_.empty()) return path;
    // 绝对路径不拼接 basePath
    if (!path.empty() && (path[0] == '/' || path[0] == '\\')) return path;
    return NormalizePath(basePath_, path);
}

bool ShaderCompiler::LoadSPIRV(const std::string& path, std::vector<std::uint8_t>& outCode) {
    lastError_.clear();
    outCode.clear();
    std::string resolved = ResolvePath(path);
    std::ifstream f(resolved, std::ios::binary | std::ios::ate);
    if (!f) {
        lastError_ = "ShaderCompiler: cannot open file: " + resolved;
        return false;
    }
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size <= 0) {
        lastError_ = "ShaderCompiler: empty or invalid file: " + resolved;
        return false;
    }
    outCode.resize(static_cast<std::size_t>(size));
    if (!f.read(reinterpret_cast<char*>(outCode.data()), size)) {
        lastError_ = "ShaderCompiler: read failed: " + resolved;
        outCode.clear();
        return false;
    }
    return true;
}

std::vector<std::uint8_t> ShaderCompiler::CompileGLSLToSPIRV(const std::string& path,
                                                              kale_device::ShaderStage stage) {
    (void)stage;
    lastError_.clear();
    // 未集成 glslang/shaderc 时：尝试约定 path + ".spv"（如 xxx.vert → xxx.vert.spv）
    std::string spvPath = path;
    if (spvPath.size() >= 4 && spvPath.compare(spvPath.size() - 4, 4, ".spv") != 0)
        spvPath += ".spv";
    std::vector<std::uint8_t> code;
    if (LoadSPIRV(spvPath, code))
        return code;
    lastError_ = "ShaderCompiler: GLSL runtime compile not available; use .spv or build-time glslc. Tried: " + spvPath;
    return {};
}

kale_device::ShaderHandle ShaderCompiler::Compile(const std::string& path,
                                                  kale_device::ShaderStage stage,
                                                  kale_device::IRenderDevice* device) {
    lastError_.clear();
    if (!device) {
        lastError_ = "ShaderCompiler: device is null";
        return kale_device::ShaderHandle{};
    }
    std::string resolved = ResolvePath(path);
    std::string ext = GetExtension(resolved);
    std::vector<std::uint8_t> code;

    if (ext == "spv") {
        if (!LoadSPIRV(resolved, code))
            return kale_device::ShaderHandle{};
    } else if (ext == "vert" || ext == "frag" || ext == "comp") {
        kale_device::ShaderStage inferred = StageFromExtension(resolved);
        code = CompileGLSLToSPIRV(resolved, inferred);
        if (code.empty())
            return kale_device::ShaderHandle{};
        stage = inferred;
    } else {
        lastError_ = "ShaderCompiler: unsupported extension '" + ext + "' for " + resolved;
        return kale_device::ShaderHandle{};
    }

    kale_device::ShaderDesc desc;
    desc.stage = stage;
    desc.code = std::move(code);
    desc.entryPoint = "main";
    kale_device::ShaderHandle h = device->CreateShader(desc);
    if (!h.IsValid())
        lastError_ = "ShaderCompiler: CreateShader failed for " + resolved;
    return h;
}

kale_device::ShaderHandle ShaderCompiler::Recompile(const std::string& path,
                                                   kale_device::ShaderStage stage,
                                                   kale_device::IRenderDevice* device) {
    return Compile(path, stage, device);
}

kale_device::ShaderStage ShaderCompiler::StageFromExtension(const std::string& path) {
    std::string ext = GetExtension(path);
    if (ext == "vert") return kale_device::ShaderStage::Vertex;
    if (ext == "frag") return kale_device::ShaderStage::Fragment;
    if (ext == "comp") return kale_device::ShaderStage::Compute;
    return kale_device::ShaderStage::Vertex;
}

bool ShaderCompiler::SupportsExtension(const std::string& path) {
    std::string ext = GetExtension(path);
    return ext == "vert" || ext == "frag" || ext == "comp" || ext == "spv";
}

}  // namespace kale::resource
