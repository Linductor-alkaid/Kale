/**
 * @file opengl_render_device.cpp
 * @brief OpenGL 后端实现（phase11-11.6）
 *
 * 使用 OpenGL 3.1 路径（GLSL 源码）；Swapchain 即默认帧缓冲；
 * 命令队列在 Submit 时于当前 GL 上下文中执行。
 * 通过 SDL_GL_GetProcAddress 加载 GL 函数（gl.h 仅提供 1.1）。
 */

#include <kale_device/opengl_render_device.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>

#include <GL/gl.h>
#ifndef GL_COPY_READ_BUFFER
#define GL_COPY_READ_BUFFER 0x8F36
#endif
#ifndef GL_COPY_WRITE_BUFFER
#define GL_COPY_WRITE_BUFFER 0x8F37
#endif

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

// 加载 OpenGL 3.x 函数（gl.h 仅 1.1）
namespace {
#define GL_PFN(ret, name, args) typedef ret (GLAPIENTRY *PFN_##name) args; static PFN_##name pfn_##name;
GL_PFN(void, GenBuffers, (GLsizei n, GLuint* buffers))
GL_PFN(void, BindBuffer, (GLenum target, GLuint buffer))
GL_PFN(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))
GL_PFN(void, BufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))
GL_PFN(void, DeleteBuffers, (GLsizei n, const GLuint* buffers))
GL_PFN(void*, MapBuffer, (GLenum target, GLenum access))
GL_PFN(GLboolean, UnmapBuffer, (GLenum target))
GL_PFN(void, GenTextures, (GLsizei n, GLuint* textures))
GL_PFN(void, BindTexture, (GLenum target, GLuint texture))
GL_PFN(void, TexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels))
GL_PFN(void, TexSubImage2D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels))
GL_PFN(void, DeleteTextures, (GLsizei n, const GLuint* textures))
GL_PFN(void, TexParameteri, (GLenum target, GLenum pname, GLint param))
GL_PFN(GLuint, CreateProgram, (void))
GL_PFN(GLuint, CreateShader, (GLenum type))
GL_PFN(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length))
GL_PFN(void, CompileShader, (GLuint shader))
GL_PFN(void, GetShaderiv, (GLuint shader, GLenum pname, GLint* params))
GL_PFN(void, GetShaderInfoLog, (GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog))
GL_PFN(void, DeleteShader, (GLuint shader))
GL_PFN(void, AttachShader, (GLuint program, GLuint shader))
GL_PFN(void, LinkProgram, (GLuint program))
GL_PFN(void, GetProgramiv, (GLuint program, GLenum pname, GLint* params))
GL_PFN(void, GetProgramInfoLog, (GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog))
GL_PFN(void, DeleteProgram, (GLuint program))
GL_PFN(void, UseProgram, (GLuint program))
GL_PFN(void, BindFramebuffer, (GLenum target, GLuint framebuffer))
GL_PFN(void, CopyBufferSubData, (GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size))
GL_PFN(void, DrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei instancecount))
GL_PFN(void, DrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void* indices, GLsizei instancecount))

bool LoadGLFunctions() {
#define LOAD(name) do { pfn_##name = (PFN_##name)SDL_GL_GetProcAddress(#name); if (!pfn_##name) return false; } while(0)
    LOAD(GenBuffers);
    LOAD(BindBuffer);
    LOAD(BufferData);
    LOAD(BufferSubData);
    LOAD(DeleteBuffers);
    LOAD(MapBuffer);
    LOAD(UnmapBuffer);
    LOAD(GenTextures);
    LOAD(BindTexture);
    LOAD(TexImage2D);
    LOAD(TexSubImage2D);
    LOAD(DeleteTextures);
    LOAD(TexParameteri);
    LOAD(CreateProgram);
    LOAD(CreateShader);
    LOAD(ShaderSource);
    LOAD(CompileShader);
    LOAD(GetShaderiv);
    LOAD(GetShaderInfoLog);
    LOAD(DeleteShader);
    LOAD(AttachShader);
    LOAD(LinkProgram);
    LOAD(GetProgramiv);
    LOAD(GetProgramInfoLog);
    LOAD(DeleteProgram);
    LOAD(UseProgram);
    LOAD(BindFramebuffer);
    LOAD(CopyBufferSubData);
    LOAD(DrawArraysInstanced);
    LOAD(DrawElementsInstanced);
#undef LOAD
    return true;
}
#undef GL_PFN
}  // namespace

namespace kale_device {

// =============================================================================
// 常量与辅助
// =============================================================================

static constexpr std::uint64_t kBackBufferTextureId = OpenGLRenderDevice::kBackBufferTextureId;

static unsigned int ToGLBufferUsage(BufferUsage u) {
    if (HasBufferUsage(u, BufferUsage::Vertex) || HasBufferUsage(u, BufferUsage::Index))
        return GL_STATIC_DRAW;
    if (HasBufferUsage(u, BufferUsage::Uniform))
        return GL_DYNAMIC_DRAW;
    return GL_STATIC_DRAW;
}

static void ToGLFormat(Format fmt, unsigned int* outInternal, unsigned int* outExternal, unsigned int* outType) {
    *outInternal = GL_RGBA8;
    *outExternal = GL_RGBA;
    *outType = GL_UNSIGNED_BYTE;
    switch (fmt) {
        case Format::R8_UNORM:   *outInternal = GL_R8;   *outExternal = GL_RED;  break;
        case Format::RG8_UNORM:  *outInternal = GL_RG8;   *outExternal = GL_RG;   break;
        case Format::RGBA8_UNORM:*outInternal = GL_RGBA8; *outExternal = GL_RGBA; break;
        case Format::RGBA8_SRGB:*outInternal = GL_SRGB8_ALPHA8; *outExternal = GL_RGBA; break;
        case Format::RGBA16F:   *outInternal = GL_RGBA16F; *outExternal = GL_RGBA; *outType = GL_HALF_FLOAT; break;
        case Format::D24:
        case Format::D32:       *outInternal = GL_DEPTH_COMPONENT24; *outExternal = GL_DEPTH_COMPONENT; *outType = GL_UNSIGNED_INT; break;
        case Format::D24S8:     *outInternal = GL_DEPTH24_STENCIL8; *outExternal = GL_DEPTH_STENCIL; *outType = GL_UNSIGNED_INT_24_8; break;
        default: break;
    }
}

// =============================================================================
// OpenGLCommandList
// =============================================================================

OpenGLCommandList::OpenGLCommandList(OpenGLRenderDevice* device) : device_(device) {}

OpenGLCommandList::~OpenGLCommandList() = default;

void OpenGLCommandList::Push(std::function<void()> cmd) {
    commands_.push_back(std::move(cmd));
}

void OpenGLCommandList::BeginRenderPass(const std::vector<TextureHandle>& colorAttachments,
                                        TextureHandle depthAttachment) {
    Push([this, colorAttachments, depthAttachment]() {
        if (!device_) return;
        (void)colorAttachments;
        (void)depthAttachment;
        device_->ApplyFramebuffer(GL_FRAMEBUFFER, 0);
    });
}

void OpenGLCommandList::EndRenderPass() {
    Push([this]() {
        if (device_) device_->ApplyFramebuffer(GL_FRAMEBUFFER, 0);
    });
}

void OpenGLCommandList::BindPipeline(PipelineHandle pipeline) {
    Push([this, pipeline]() {
        if (!device_ || !pipeline.IsValid()) return;
        auto it = device_->pipelines_.find(pipeline.id);
        if (it != device_->pipelines_.end())
            device_->ApplyProgram(it->second.glProgram);
    });
}

void OpenGLCommandList::BindDescriptorSet(std::uint32_t set, DescriptorSetHandle descriptorSet) {
    Push([this, set, descriptorSet]() {
        if (!device_ || !descriptorSet.IsValid()) return;
        auto it = device_->descriptorSets_.find(descriptorSet.id);
        if (it == device_->descriptorSets_.end()) return;
        int unit = 0;
        for (const auto& [binding, tex] : it->second.textures) {
            if (!tex.IsValid()) continue;
            if (tex.id == kBackBufferTextureId) continue;
            auto tit = device_->textures_.find(tex.id);
            if (tit != device_->textures_.end()) {
                device_->ApplyTexture2D(unit, tit->second.glTexture);
                unit++;
            }
        }
        for (const auto& [binding, p] : it->second.buffers) {
            if (!p.first.IsValid()) continue;
            auto bit = device_->buffers_.find(p.first.id);
            if (bit != device_->buffers_.end())
                device_->ApplyBuffer(GL_UNIFORM_BUFFER, bit->second.glBuffer);
        }
    });
    (void)set;
}

void OpenGLCommandList::BindVertexBuffer(std::uint32_t binding, BufferHandle buffer, std::size_t offset) {
    (void)offset;
    (void)binding;
    Push([this, buffer]() {
        if (!device_ || !buffer.IsValid()) return;
        auto it = device_->buffers_.find(buffer.id);
        if (it != device_->buffers_.end())
            device_->ApplyBuffer(GL_ARRAY_BUFFER, it->second.glBuffer);
    });
}

void OpenGLCommandList::BindIndexBuffer(BufferHandle buffer, std::size_t offset, bool is16Bit) {
    (void)offset;
    Push([this, buffer, is16Bit]() {
        if (!device_ || !buffer.IsValid()) return;
        auto it = device_->buffers_.find(buffer.id);
        if (it != device_->buffers_.end()) {
            device_->ApplyBuffer(GL_ELEMENT_ARRAY_BUFFER, it->second.glBuffer);
            (void)is16Bit;
        }
    });
}

void OpenGLCommandList::SetPushConstants(const void* data, std::size_t size, std::size_t offset) {
    if (!data || size == 0) return;
    std::vector<std::uint8_t> copy(static_cast<const std::uint8_t*>(data) + offset,
                                   static_cast<const std::uint8_t*>(data) + offset + size);
    Push([copy]() {
        (void)copy;
        // 需与 pipeline 的 uniform 布局一致；简化实现可忽略或通过当前 program 的 uniform 上传
    });
}

void OpenGLCommandList::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount,
                             std::uint32_t firstVertex, std::uint32_t firstInstance) {
    Push([vertexCount, instanceCount, firstVertex, firstInstance]() {
        if (instanceCount <= 1)
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(firstVertex), static_cast<GLsizei>(vertexCount));
        else
            pfn_DrawArraysInstanced(GL_TRIANGLES, static_cast<GLint>(firstVertex),
                                   static_cast<GLsizei>(vertexCount), static_cast<GLsizei>(instanceCount));
    });
}

void OpenGLCommandList::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
                                    std::uint32_t firstIndex, std::int32_t vertexOffset,
                                    std::uint32_t firstInstance) {
    (void)vertexOffset;
    (void)firstInstance;
    Push([indexCount, instanceCount, firstIndex]() {
        if (instanceCount <= 1)
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
                           reinterpret_cast<void*>(static_cast<uintptr_t>(firstIndex * 4u)));
        else
            pfn_DrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
                                     reinterpret_cast<void*>(static_cast<uintptr_t>(firstIndex * 4u)),
                                     static_cast<GLsizei>(instanceCount));
    });
}

void OpenGLCommandList::Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) {
    // OpenGL 2.1 无 Compute；留空
}

void OpenGLCommandList::CopyBufferToBuffer(BufferHandle srcBuffer, std::size_t srcOffset,
                                           BufferHandle dstBuffer, std::size_t dstOffset, std::size_t size) {
    Push([this, srcBuffer, srcOffset, dstBuffer, dstOffset, size]() {
        if (!device_) return;
        auto sit = device_->buffers_.find(srcBuffer.id);
        auto dit = device_->buffers_.find(dstBuffer.id);
        if (sit == device_->buffers_.end() || dit == device_->buffers_.end()) return;
        pfn_BindBuffer(GL_COPY_READ_BUFFER, sit->second.glBuffer);
        pfn_BindBuffer(GL_COPY_WRITE_BUFFER, dit->second.glBuffer);
        pfn_CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                              static_cast<GLintptr>(srcOffset), static_cast<GLintptr>(dstOffset),
                              static_cast<GLsizeiptr>(size));
        pfn_BindBuffer(GL_COPY_READ_BUFFER, 0);
        pfn_BindBuffer(GL_COPY_WRITE_BUFFER, 0);
    });
}

void OpenGLCommandList::CopyBufferToTexture(BufferHandle, std::size_t, TextureHandle,
                                            std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {
    // 简化：可留空或实现 glTexSubImage2D 从 PBO
}

void OpenGLCommandList::CopyTextureToTexture(TextureHandle srcTexture, TextureHandle dstTexture,
                                             std::uint32_t width, std::uint32_t height) {
    (void)srcTexture;
    (void)dstTexture;
    (void)width;
    (void)height;
    // OpenGL 后端：Texture→BackBuffer 需 FBO 绑定源纹理，此处留空；调用方可用全屏四边形绘制代替
    Push([]() {});
}

void OpenGLCommandList::Barrier(const std::vector<TextureHandle>&) {
    Push([]() { glFlush(); });
}

void OpenGLCommandList::ClearColor(TextureHandle texture, const float color[4]) {
    if (!color) return;
    std::array<float, 4> c = { color[0], color[1], color[2], color[3] };
    Push([this, texture, c]() {
        if (texture.id == kBackBufferTextureId || texture.id == 0) {
            glClearColor(c[0], c[1], c[2], c[3]);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    });
}

void OpenGLCommandList::ClearDepth(TextureHandle texture, float depth, std::uint8_t stencil) {
    Push([texture, depth, stencil]() {
        if (texture.id == kBackBufferTextureId || texture.id == 0) {
            glClearDepth(static_cast<double>(depth));
            glClearStencil(stencil);
            glClear(GL_DEPTH_BUFFER_BIT | (stencil ? GL_STENCIL_BUFFER_BIT : 0));
        }
        (void)stencil;
    });
}

void OpenGLCommandList::SetViewport(float x, float y, float width, float height,
                                    float minDepth, float maxDepth) {
    Push([x, y, width, height, minDepth, maxDepth]() {
        glViewport(static_cast<GLint>(x), static_cast<GLint>(y),
                   static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        glDepthRange(static_cast<GLclampd>(minDepth), static_cast<GLclampd>(maxDepth));
    });
}

void OpenGLCommandList::SetScissor(std::int32_t x, std::int32_t y, std::uint32_t width, std::uint32_t height) {
    Push([x, y, width, height]() {
        glScissor(x, y, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    });
}

void OpenGLCommandList::Execute() {
    if (!device_) return;
    device_->EnsureContext();
    for (auto& fn : commands_)
        fn();
}

// =============================================================================
// OpenGLRenderDevice
// =============================================================================

OpenGLRenderDevice::~OpenGLRenderDevice() {
    Shutdown();
}

bool OpenGLRenderDevice::MakeCurrent() {
    if (!window_ || !glContext_) return false;
    return SDL_GL_MakeCurrent(static_cast<SDL_Window*>(window_), static_cast<SDL_GLContext>(glContext_)) == 0;
}

void OpenGLRenderDevice::EnsureContext() {
    MakeCurrent();
}

void OpenGLRenderDevice::ApplyProgram(unsigned int program) {
    if (glStateCache_.boundProgram == program) return;
    glStateCache_.boundProgram = program;
    pfn_UseProgram(static_cast<GLuint>(program));
}

void OpenGLRenderDevice::ApplyTexture2D(int unit, unsigned int texture) {
    if (unit < 0 || unit >= GLStateCache::kMaxTextureUnits) return;
    if (glStateCache_.boundTexture2D[unit] == texture) return;
    glStateCache_.boundTexture2D[unit] = texture;
    if (glStateCache_.activeTextureUnit != unit) {
        glStateCache_.activeTextureUnit = unit;
        glActiveTexture(GL_TEXTURE0 + unit);
    }
    pfn_BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
}

void OpenGLRenderDevice::ApplyBuffer(unsigned int target, unsigned int buffer) {
    if (target == GL_ARRAY_BUFFER) {
        if (glStateCache_.boundArrayBuffer == buffer) return;
        glStateCache_.boundArrayBuffer = buffer;
    } else if (target == GL_ELEMENT_ARRAY_BUFFER) {
        if (glStateCache_.boundElementArrayBuffer == buffer) return;
        glStateCache_.boundElementArrayBuffer = buffer;
    } else if (target == GL_UNIFORM_BUFFER) {
        if (glStateCache_.boundUniformBuffer == buffer) return;
        glStateCache_.boundUniformBuffer = buffer;
    } else {
        pfn_BindBuffer(static_cast<GLenum>(target), static_cast<GLuint>(buffer));
        return;
    }
    pfn_BindBuffer(static_cast<GLenum>(target), static_cast<GLuint>(buffer));
}

void OpenGLRenderDevice::ApplyFramebuffer(unsigned int target, unsigned int framebuffer) {
    if (glStateCache_.boundFramebuffer == framebuffer) return;
    glStateCache_.boundFramebuffer = framebuffer;
    pfn_BindFramebuffer(static_cast<GLenum>(target), static_cast<GLuint>(framebuffer));
}

void OpenGLRenderDevice::InvalidateBufferInCache(unsigned int glBuffer) {
    if (glStateCache_.boundArrayBuffer == glBuffer) glStateCache_.boundArrayBuffer = 0;
    if (glStateCache_.boundElementArrayBuffer == glBuffer) glStateCache_.boundElementArrayBuffer = 0;
    if (glStateCache_.boundUniformBuffer == glBuffer) glStateCache_.boundUniformBuffer = 0;
}

void OpenGLRenderDevice::InvalidateTextureInCache(unsigned int glTexture) {
    for (int i = 0; i < GLStateCache::kMaxTextureUnits; ++i) {
        if (glStateCache_.boundTexture2D[i] == glTexture) glStateCache_.boundTexture2D[i] = 0;
    }
}

void OpenGLRenderDevice::InvalidateProgramInCache(unsigned int glProgram) {
    if (glStateCache_.boundProgram == glProgram) glStateCache_.boundProgram = 0;
}

bool OpenGLRenderDevice::Initialize(const DeviceConfig& config) {
    if (!config.windowHandle || config.width == 0 || config.height == 0) {
        lastError_ = "OpenGL: invalid config (window or size)";
        return false;
    }
    window_ = config.windowHandle;
    width_ = config.width;
    height_ = config.height;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    SDL_GLContext ctx = SDL_GL_CreateContext(static_cast<SDL_Window*>(window_));
    if (!ctx) {
        lastError_ = std::string("SDL_GL_CreateContext: ") + SDL_GetError();
        return false;
    }
    glContext_ = ctx;
    if (!MakeCurrent()) {
        lastError_ = "SDL_GL_MakeCurrent failed";
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
        glContext_ = nullptr;
        return false;
    }
    if (!LoadGLFunctions()) {
        lastError_ = "OpenGL: failed to load GL functions (need 3.1+)";
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
        glContext_ = nullptr;
        return false;
    }
    SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);

    capabilities_.maxTextureSize = 4096;
    capabilities_.maxComputeWorkGroupSize[0] = capabilities_.maxComputeWorkGroupSize[1] = capabilities_.maxComputeWorkGroupSize[2] = 1;
    capabilities_.supportsGeometryShader = false;
    capabilities_.supportsTessellation = false;
    capabilities_.supportsComputeShader = false;
    capabilities_.supportsRayTracing = false;
    capabilities_.maxRecordingThreads = 1;

    return true;
}

void OpenGLRenderDevice::Shutdown() {
    if (!glContext_) return;
    EnsureContext();
    for (auto& [id, res] : buffers_) {
        if (res.glBuffer) pfn_DeleteBuffers(1, &res.glBuffer);
    }
    buffers_.clear();
    for (auto& [id, res] : textures_) {
        if (res.glTexture) pfn_DeleteTextures(1, &res.glTexture);
    }
    textures_.clear();
    for (auto& [id, res] : shaders_) {
        if (res.glShader) pfn_DeleteShader(res.glShader);
    }
    shaders_.clear();
    for (auto& [id, res] : pipelines_) {
        if (res.glProgram) pfn_DeleteProgram(res.glProgram);
    }
    pipelines_.clear();
    descriptorSets_.clear();
    for (auto& [id, res] : fences_) {
        if (res.glSync) {
            // GLsync 需在 GL 3.2+ 用 glDeleteSync
            (void)res.glSync;
        }
    }
    fences_.clear();

    if (glContext_) {
        SDL_GL_DestroyContext(static_cast<SDL_GLContext>(glContext_));
        glContext_ = nullptr;
    }
    window_ = nullptr;
    lastError_.clear();
    capabilities_ = DeviceCapabilities{};
}

const std::string& OpenGLRenderDevice::GetLastError() const {
    return lastError_;
}

BufferHandle OpenGLRenderDevice::CreateBuffer(const BufferDesc& desc, const void* data) {
    if (desc.size == 0) return BufferHandle{};
    EnsureContext();
    unsigned int glBuf = 0;
    pfn_GenBuffers(1, &glBuf);
    if (!glBuf) return BufferHandle{};
    unsigned int target = HasBufferUsage(desc.usage, BufferUsage::Index) ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    if (HasBufferUsage(desc.usage, BufferUsage::Uniform)) target = GL_UNIFORM_BUFFER;
    pfn_BindBuffer(target, glBuf);
    pfn_BufferData(target, static_cast<GLsizeiptr>(desc.size), data, ToGLBufferUsage(desc.usage));
    pfn_BindBuffer(target, 0);
    if (target == GL_ARRAY_BUFFER) glStateCache_.boundArrayBuffer = 0;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) glStateCache_.boundElementArrayBuffer = 0;
    else if (target == GL_UNIFORM_BUFFER) glStateCache_.boundUniformBuffer = 0;

    std::uint64_t id = NextId();
    buffers_[id] = { glBuf, desc.size, desc.cpuVisible };
    BufferHandle h;
    h.id = id;
    return h;
}

TextureHandle OpenGLRenderDevice::CreateTexture(const TextureDesc& desc, const void* data) {
    if (desc.width == 0 || desc.height == 0) return TextureHandle{};
    EnsureContext();
    unsigned int internalFormat = GL_RGBA8, external = GL_RGBA, type = GL_UNSIGNED_BYTE;
    ToGLFormat(desc.format, &internalFormat, &external, &type);

    unsigned int glTex = 0;
    pfn_GenTextures(1, &glTex);
    if (!glTex) return TextureHandle{};
    ApplyTexture2D(0, glTex);
    pfn_TexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat),
                   static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height), 0,
                   external, type, data);
    pfn_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    pfn_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ApplyTexture2D(0, 0);

    std::uint64_t id = NextId();
    textures_[id] = { glTex, desc.width, desc.height };
    TextureHandle h;
    h.id = id;
    return h;
}

ShaderHandle OpenGLRenderDevice::CreateShader(const ShaderDesc& desc) {
    if (desc.code.empty()) return ShaderHandle{};
    EnsureContext();
    GLenum glStage = GL_VERTEX_SHADER;
    if (desc.stage == ShaderStage::Fragment) glStage = GL_FRAGMENT_SHADER;
    else if (desc.stage == ShaderStage::Compute) glStage = GL_COMPUTE_SHADER;

    // SPIR-V magic: 0x07230203 -> OpenGL 要求 GLSL 源码
    const std::uint8_t* p = desc.code.data();
    if (desc.code.size() >= 4 && p[0] == 0x03 && p[1] == 0x02 && p[2] == 0x23 && p[3] == 0x07) {
        lastError_ = "OpenGL backend expects GLSL source, not SPIR-V";
        return ShaderHandle{};
    }
    std::string src(desc.code.begin(), desc.code.end());
    const char* srcPtr = src.c_str();
    GLint len = static_cast<GLint>(src.size());

    GLuint sh = pfn_CreateShader(glStage);
    if (!sh) return ShaderHandle{};
    pfn_ShaderSource(sh, 1, &srcPtr, &len);
    pfn_CompileShader(sh);
    GLint ok = 0;
    pfn_GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        pfn_GetShaderInfoLog(sh, sizeof(log), nullptr, log);
        lastError_ = std::string("glCompileShader: ") + log;
        pfn_DeleteShader(sh);
        return ShaderHandle{};
    }

    std::uint64_t id = NextId();
    shaders_[id] = { sh, desc.stage };
    ShaderHandle h;
    h.id = id;
    return h;
}

PipelineHandle OpenGLRenderDevice::CreatePipeline(const PipelineDesc& desc) {
    if (desc.shaders.empty()) return PipelineHandle{};
    EnsureContext();
    GLuint prog = pfn_CreateProgram();
    if (!prog) return PipelineHandle{};
    for (const auto& shH : desc.shaders) {
        if (!shH.IsValid()) continue;
        auto it = shaders_.find(shH.id);
        if (it != shaders_.end())
            pfn_AttachShader(prog, it->second.glShader);
    }
    pfn_LinkProgram(prog);
    GLint ok = 0;
    pfn_GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        pfn_GetProgramInfoLog(prog, sizeof(log), nullptr, log);
        lastError_ = std::string("glLinkProgram: ") + log;
        pfn_DeleteProgram(prog);
        return PipelineHandle{};
    }

    std::uint64_t id = NextId();
    pipelines_[id] = { prog };
    PipelineHandle h;
    h.id = id;
    return h;
}

DescriptorSetHandle OpenGLRenderDevice::CreateDescriptorSet(const DescriptorSetLayoutDesc& layout) {
    (void)layout;
    std::uint64_t id = NextId();
    descriptorSets_[id] = {};
    DescriptorSetHandle h;
    h.id = id;
    return h;
}

void OpenGLRenderDevice::WriteDescriptorSetTexture(DescriptorSetHandle set, std::uint32_t binding,
                                                    TextureHandle texture) {
    if (!set.IsValid()) return;
    descriptorSets_[set.id].textures[binding] = texture;
}

void OpenGLRenderDevice::WriteDescriptorSetBuffer(DescriptorSetHandle set, std::uint32_t binding,
                                                  BufferHandle buffer, std::size_t offset, std::size_t range) {
    if (!set.IsValid()) return;
    descriptorSets_[set.id].buffers[binding] = { buffer, offset };
    (void)range;
}

void OpenGLRenderDevice::DestroyBuffer(BufferHandle handle) {
    if (!handle.IsValid()) return;
    auto it = buffers_.find(handle.id);
    if (it != buffers_.end()) {
        if (it->second.glBuffer) {
            InvalidateBufferInCache(it->second.glBuffer);
            pfn_DeleteBuffers(1, &it->second.glBuffer);
        }
        buffers_.erase(it);
    }
}

void OpenGLRenderDevice::DestroyTexture(TextureHandle handle) {
    if (!handle.IsValid() || handle.id == kBackBufferTextureId) return;
    auto it = textures_.find(handle.id);
    if (it != textures_.end()) {
        if (it->second.glTexture) {
            InvalidateTextureInCache(it->second.glTexture);
            pfn_DeleteTextures(1, &it->second.glTexture);
        }
        textures_.erase(it);
    }
}

void OpenGLRenderDevice::DestroyShader(ShaderHandle handle) {
    if (!handle.IsValid()) return;
    auto it = shaders_.find(handle.id);
    if (it != shaders_.end()) {
        if (it->second.glShader) pfn_DeleteShader(it->second.glShader);
        shaders_.erase(it);
    }
}

void OpenGLRenderDevice::DestroyPipeline(PipelineHandle handle) {
    if (!handle.IsValid()) return;
    auto it = pipelines_.find(handle.id);
    if (it != pipelines_.end()) {
        if (it->second.glProgram) {
            InvalidateProgramInCache(it->second.glProgram);
            pfn_DeleteProgram(it->second.glProgram);
        }
        pipelines_.erase(it);
    }
}

void OpenGLRenderDevice::DestroyDescriptorSet(DescriptorSetHandle handle) {
    if (!handle.IsValid()) return;
    descriptorSets_.erase(handle.id);
}

void OpenGLRenderDevice::UpdateBuffer(BufferHandle handle, const void* data, std::size_t size, std::size_t offset) {
    if (!handle.IsValid() || !data) return;
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end()) return;
    EnsureContext();
    ApplyBuffer(GL_ARRAY_BUFFER, it->second.glBuffer);
    pfn_BufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(size), data);
    ApplyBuffer(GL_ARRAY_BUFFER, 0);
}

void* OpenGLRenderDevice::MapBuffer(BufferHandle handle, std::size_t offset, std::size_t size) {
    if (!handle.IsValid()) return nullptr;
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end() || !it->second.cpuVisible) return nullptr;
    EnsureContext();
    ApplyBuffer(GL_ARRAY_BUFFER, it->second.glBuffer);
    void* ptr = pfn_MapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    ApplyBuffer(GL_ARRAY_BUFFER, 0);
    return ptr ? static_cast<char*>(ptr) + offset : nullptr;
}

void OpenGLRenderDevice::UnmapBuffer(BufferHandle handle) {
    if (!handle.IsValid()) return;
    auto it = buffers_.find(handle.id);
    if (it == buffers_.end()) return;
    EnsureContext();
    ApplyBuffer(GL_ARRAY_BUFFER, it->second.glBuffer);
    pfn_UnmapBuffer(GL_ARRAY_BUFFER);
    ApplyBuffer(GL_ARRAY_BUFFER, 0);
}

void OpenGLRenderDevice::UpdateTexture(TextureHandle handle, const void* data, std::uint32_t mipLevel) {
    if (!handle.IsValid() || handle.id == kBackBufferTextureId || !data) return;
    auto it = textures_.find(handle.id);
    if (it == textures_.end()) return;
    EnsureContext();
    ApplyTexture2D(0, it->second.glTexture);
    pfn_TexSubImage2D(GL_TEXTURE_2D, static_cast<GLint>(mipLevel), 0, 0,
                      static_cast<GLsizei>(it->second.width), static_cast<GLsizei>(it->second.height),
                      GL_RGBA, GL_UNSIGNED_BYTE, data);
    ApplyTexture2D(0, 0);
}

CommandList* OpenGLRenderDevice::BeginCommandList(std::uint32_t threadIndex) {
    (void)threadIndex;
    EnsureContext();
    if (activeCommandList_) return nullptr;
    activeCommandList_ = new OpenGLCommandList(this);
    return activeCommandList_;
}

void OpenGLRenderDevice::EndCommandList(CommandList* cmd) {
    if (cmd == activeCommandList_)
        activeCommandList_ = nullptr;
}

void OpenGLRenderDevice::Submit(const std::vector<CommandList*>& cmdLists,
                                const std::vector<SemaphoreHandle>&,
                                const std::vector<SemaphoreHandle>&,
                                FenceHandle fence) {
    EnsureContext();
    for (CommandList* cmd : cmdLists) {
        auto* glCmd = dynamic_cast<OpenGLCommandList*>(cmd);
        if (glCmd) glCmd->Execute();
    }
    if (fence.IsValid()) {
        fences_[fence.id].glSync = nullptr;  // 可扩展为 glFenceSync
    }
    currentFrameIndex_ = (currentFrameIndex_ + 1) % 3;
}

void OpenGLRenderDevice::WaitIdle() {
    EnsureContext();
    glFinish();
}

FenceHandle OpenGLRenderDevice::CreateFence(bool signaled) {
    (void)signaled;
    std::uint64_t id = NextId();
    fences_[id] = {};
    FenceHandle h;
    h.id = id;
    return h;
}

void OpenGLRenderDevice::WaitForFence(FenceHandle fence, std::uint64_t timeout) {
    (void)timeout;
    if (!fence.IsValid()) return;
    glFinish();
}

void OpenGLRenderDevice::ResetFence(FenceHandle fence) {
    if (!fence.IsValid()) return;
    fences_[fence.id].glSync = nullptr;
}

bool OpenGLRenderDevice::IsFenceSignaled(FenceHandle fence) const {
    if (!fence.IsValid()) return true;
    auto it = fences_.find(fence.id);
    return it == fences_.end() || !it->second.glSync;
}

SemaphoreHandle OpenGLRenderDevice::CreateSemaphore() {
    SemaphoreHandle h;
    h.id = NextId();
    return h;
}

std::uint32_t OpenGLRenderDevice::AcquireNextImage() {
    return 0;
}

void OpenGLRenderDevice::Present() {
    if (window_)
        SDL_GL_SwapWindow(static_cast<SDL_Window*>(window_));
}

TextureHandle OpenGLRenderDevice::GetBackBuffer() {
    TextureHandle h;
    h.id = kBackBufferTextureId;
    return h;
}

std::uint32_t OpenGLRenderDevice::GetCurrentFrameIndex() const {
    return currentFrameIndex_;
}

void OpenGLRenderDevice::SetExtent(std::uint32_t width, std::uint32_t height) {
    width_ = width;
    height_ = height;
}

const DeviceCapabilities& OpenGLRenderDevice::GetCapabilities() const {
    return capabilities_;
}

}  // namespace kale_device
