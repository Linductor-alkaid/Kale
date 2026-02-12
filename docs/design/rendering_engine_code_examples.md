# 渲染引擎核心代码设计示例

> 本文档与 [rendering_engine_design.md](./rendering_engine_design.md) 设计对齐，采用 Scene Graph（Transform）+ ECS（逻辑）+ SceneNodeRef（桥接）架构。

## 目录
1. [核心类设计](#核心类设计)
2. [完整使用示例](#完整使用示例)
3. [性能优化技巧](#性能优化技巧)
4. [常见问题和解决方案](#常见问题和解决方案)

---

## 核心类设计

### 1. 设备抽象层完整接口

```cpp
// RenderDevice.h
#pragma once
#include <memory>
#include <vector>
#include <string>
#include <cstdint>

namespace RenderEngine {

// 资源句柄类型安全封装
template<typename T>
struct Handle {
    uint64_t id = 0;
    
    bool IsValid() const { return id != 0; }
    bool operator==(const Handle& other) const { return id == other.id; }
    bool operator!=(const Handle& other) const { return id != other.id; }
};

using BufferHandle = Handle<struct Buffer_Tag>;
using TextureHandle = Handle<struct Texture_Tag>;
using ShaderHandle = Handle<struct Shader_Tag>;
using PipelineHandle = Handle<struct Pipeline_Tag>;
using DescriptorSetHandle = Handle<struct DescriptorSet_Tag>;
using FenceHandle = Handle<struct Fence_Tag>;
using SemaphoreHandle = Handle<struct Semaphore_Tag>;

// 枚举定义
enum class Format {
    Undefined,
    // Color formats
    R8_UNORM, RG8_UNORM, RGBA8_UNORM, RGBA8_SRGB,
    R16F, RG16F, RGBA16F,
    R32F, RG32F, RGB32F, RGBA32F,
    // Depth formats
    D16, D24, D32, D24S8, D32S8,
    // Compressed formats
    BC1, BC3, BC5, BC7,
};

enum class BufferUsage {
    Vertex      = 1 << 0,
    Index       = 1 << 1,
    Uniform     = 1 << 2,
    Storage     = 1 << 3,
    Transfer    = 1 << 4,
};

enum class TextureUsage {
    Sampled         = 1 << 0,
    Storage         = 1 << 1,
    ColorAttachment = 1 << 2,
    DepthAttachment = 1 << 3,
    Transfer        = 1 << 4,
};

enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
    Geometry,
    TessControl,
    TessEvaluation,
};

enum class PrimitiveTopology {
    TriangleList,
    TriangleStrip,
    LineList,
    PointList,
};

enum class CompareOp {
    Never, Less, Equal, LessOrEqual,
    Greater, NotEqual, GreaterOrEqual, Always,
};

enum class BlendFactor {
    Zero, One,
    SrcColor, OneMinusSrcColor,
    DstColor, OneMinusDstColor,
    SrcAlpha, OneMinusSrcAlpha,
    DstAlpha, OneMinusDstAlpha,
};

enum class BlendOp {
    Add, Subtract, ReverseSubtract, Min, Max,
};

// 资源描述符
struct BufferDesc {
    size_t size;
    BufferUsage usage;
    bool cpuVisible = false;
};

struct TextureDesc {
    uint32_t width;
    uint32_t height;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    Format format;
    TextureUsage usage;
    bool isCube = false;
};

struct ShaderDesc {
    ShaderStage stage;
    std::vector<uint8_t> code; // SPIR-V or GLSL
    std::string entryPoint = "main";
};

struct BlendState {
    bool blendEnable = false;
    BlendFactor srcColorBlendFactor = BlendFactor::One;
    BlendFactor dstColorBlendFactor = BlendFactor::Zero;
    BlendOp colorBlendOp = BlendOp::Add;
    BlendFactor srcAlphaBlendFactor = BlendFactor::One;
    BlendFactor dstAlphaBlendFactor = BlendFactor::Zero;
    BlendOp alphaBlendOp = BlendOp::Add;
};

struct DepthStencilState {
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    CompareOp depthCompareOp = CompareOp::Less;
    bool stencilTestEnable = false;
};

struct RasterizationState {
    bool cullEnable = true;
    bool frontFaceCCW = true;
    float lineWidth = 1.0f;
};

struct VertexInputBinding {
    uint32_t binding;
    uint32_t stride;
    bool perInstance = false;
};

struct VertexInputAttribute {
    uint32_t location;
    uint32_t binding;
    Format format;
    uint32_t offset;
};

struct PipelineDesc {
    std::vector<ShaderHandle> shaders;
    std::vector<VertexInputBinding> vertexBindings;
    std::vector<VertexInputAttribute> vertexAttributes;
    
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    RasterizationState rasterization;
    DepthStencilState depthStencil;
    std::vector<BlendState> blendStates;
    
    std::vector<Format> colorAttachmentFormats;
    Format depthAttachmentFormat = Format::Undefined;
};

// 命令列表接口
class CommandList {
public:
    virtual ~CommandList() = default;
    
    // Render Pass
    virtual void BeginRenderPass(const std::vector<TextureHandle>& colorAttachments,
                                 TextureHandle depthAttachment = {}) = 0;
    virtual void EndRenderPass() = 0;
    
    // Pipeline Binding
    virtual void BindPipeline(PipelineHandle pipeline) = 0;
    virtual void BindDescriptorSet(uint32_t set, DescriptorSetHandle descriptorSet) = 0;
    
    // Resource Binding
    virtual void BindVertexBuffer(uint32_t binding, BufferHandle buffer, size_t offset = 0) = 0;
    virtual void BindIndexBuffer(BufferHandle buffer, size_t offset = 0, bool is16Bit = false) = 0;
    
    // Push Constants
    virtual void SetPushConstants(const void* data, size_t size, size_t offset = 0) = 0;
    
    // Draw Commands
    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                     uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                            uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                            uint32_t firstInstance = 0) = 0;
    
    // Compute
    virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
    
    // Resource Barriers
    virtual void Barrier(const std::vector<TextureHandle>& textures) = 0;
    
    // Clear
    virtual void ClearColor(TextureHandle texture, const float color[4]) = 0;
    virtual void ClearDepth(TextureHandle texture, float depth, uint8_t stencil = 0) = 0;
    
    // Viewport and Scissor
    virtual void SetViewport(float x, float y, float width, float height,
                            float minDepth = 0.0f, float maxDepth = 1.0f) = 0;
    virtual void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;
};

// 渲染设备接口
class IRenderDevice {
public:
    virtual ~IRenderDevice() = default;
    
    // 初始化和清理
    virtual bool Initialize(const struct DeviceConfig& config) = 0;
    virtual void Shutdown() = 0;
    
    // 资源创建
    virtual BufferHandle CreateBuffer(const BufferDesc& desc, const void* data = nullptr) = 0;
    virtual TextureHandle CreateTexture(const TextureDesc& desc, const void* data = nullptr) = 0;
    virtual ShaderHandle CreateShader(const ShaderDesc& desc) = 0;
    virtual PipelineHandle CreatePipeline(const PipelineDesc& desc) = 0;
    virtual DescriptorSetHandle CreateDescriptorSet(/* layout */) = 0;
    
    // 资源销毁
    virtual void DestroyBuffer(BufferHandle handle) = 0;
    virtual void DestroyTexture(TextureHandle handle) = 0;
    virtual void DestroyShader(ShaderHandle handle) = 0;
    virtual void DestroyPipeline(PipelineHandle handle) = 0;
    
    // 资源更新
    virtual void UpdateBuffer(BufferHandle handle, const void* data, size_t size, size_t offset = 0) = 0;
    virtual void UpdateTexture(TextureHandle handle, const void* data, uint32_t mipLevel = 0) = 0;
    
    // 命令录制
    virtual CommandList* BeginCommandList() = 0;
    virtual void EndCommandList(CommandList* cmd) = 0;
    virtual void Submit(const std::vector<CommandList*>& cmdLists,
                       const std::vector<SemaphoreHandle>& waitSemaphores = {},
                       const std::vector<SemaphoreHandle>& signalSemaphores = {},
                       FenceHandle fence = {}) = 0;
    
    // 同步
    virtual void WaitIdle() = 0;
    virtual FenceHandle CreateFence(bool signaled = false) = 0;
    virtual void WaitForFence(FenceHandle fence, uint64_t timeout = UINT64_MAX) = 0;
    virtual void ResetFence(FenceHandle fence) = 0;
    virtual SemaphoreHandle CreateSemaphore() = 0;
    
    // 交换链
    virtual void Present() = 0;
    virtual TextureHandle GetBackBuffer() = 0;
    virtual uint32_t GetCurrentFrameIndex() const = 0;
    
    // 查询
    virtual const struct DeviceCapabilities& GetCapabilities() const = 0;
};

struct DeviceConfig {
    void* windowHandle;
    uint32_t width;
    uint32_t height;
    bool enableValidation = false;
    bool vsync = true;
    uint32_t backBufferCount = 3;
};

struct DeviceCapabilities {
    uint32_t maxTextureSize;
    uint32_t maxComputeWorkGroupSize[3];
    bool supportsGeometryShader;
    bool supportsTessellation;
    bool supportsComputeShader;
    bool supportsRayTracing;
};

} // namespace RenderEngine
```

### 2. 场景图与 SceneNodeRef（生命周期安全）

```cpp
// SceneGraph.h - 依设计：Scene Graph 负责 Transform，ECS 通过 SceneNodeRef 桥接
#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace RenderEngine {

// 场景节点句柄（不透明，由 SceneManager 分配；节点销毁时失效）
using SceneNodeHandle = uint64_t;
constexpr SceneNodeHandle kInvalidSceneNodeHandle = 0;

// 前向声明
class SceneManager;
class SceneNode;

// 安全的场景节点引用组件（ECS 到 Scene Graph 的桥接）
struct SceneNodeRef {
    SceneNodeHandle handle = kInvalidSceneNodeHandle;
    
    bool IsValid() const { return handle != kInvalidSceneNodeHandle; }
    
    SceneNode* GetNode(SceneManager* sceneMgr) const;
    
    static SceneNodeRef FromNode(SceneNode* node) {
        SceneNodeRef ref;
        ref.handle = node ? node->GetHandle() : kInvalidSceneNodeHandle;
        return ref;
    }
};

// Pass 标志
enum PassFlags : uint32_t {
    ShadowCaster = 1,
    Opaque = 2,
    Transparent = 4,
    All = ShadowCaster | Opaque | Transparent
};

class SceneNode {
public:
    void SetLocalTransform(const glm::mat4& m);
    const glm::mat4& GetLocalTransform() const;
    const glm::mat4& GetWorldMatrix() const;
    
    SceneNode* AddChild(std::unique_ptr<SceneNode> child);
    void SetRenderable(class Renderable* r);
    Renderable* GetRenderable() const;
    void SetPassFlags(PassFlags f) { passFlags = f; }
    PassFlags GetPassFlags() const { return passFlags; }
    
    SceneNodeHandle GetHandle() const { return handle_; }  // 创建时由 SceneManager 分配

private:
    SceneNodeHandle handle_ = kInvalidSceneNodeHandle;
    glm::mat4 localTransform_{1.0f};
    glm::mat4 worldMatrix_{1.0f};
    std::vector<std::unique_ptr<SceneNode>> children;
    Renderable* renderable = nullptr;
    PassFlags passFlags = PassFlags::All;
    SceneNode* parent = nullptr;
    friend class SceneManager;
};

class CameraNode : public SceneNode {
public:
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
};

class SceneManager {
public:
    SceneNode* CreateScene();
    void Update(float deltaTime);
    void SetActiveScene(SceneNode* root);
    SceneNodeHandle GetHandle(SceneNode* node) const;
    SceneNode* GetNode(SceneNodeHandle handle) const;
    std::vector<SceneNode*> CullScene(CameraNode* camera);

private:
    std::unordered_map<SceneNodeHandle, SceneNode*> handleRegistry_;
    SceneNode* activeRoot_ = nullptr;
    uint64_t nextHandle_ = 1;
};

// SceneNodeRef::GetNode 实现
inline SceneNode* SceneNodeRef::GetNode(SceneManager* sceneMgr) const {
    return sceneMgr ? sceneMgr->GetNode(handle) : nullptr;
}

} // namespace RenderEngine
```

### 3. ECS 系统（逻辑层，与 Scene Graph 桥接）

```cpp
// ECS.h - 依设计：ECS 仅负责游戏逻辑，Transform 由 Scene Graph 管理
#pragma once
#include <vector>
#include <unordered_map>
#include <memory>
#include <typeindex>
#include <glm/glm.hpp>

namespace RenderEngine {

struct Entity {
    uint32_t id = 0;
    uint32_t generation = 0;
    static const Entity Null;
    bool IsValid() const { return id != 0; }
};

// 逻辑组件（不含 Transform，Transform 在 Scene Graph）
struct PhysicsComponent {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
};

struct PlayerController {
    float moveSpeed = 5.0f;
    float jumpForce = 8.0f;
};

// 组件存储 (AoS)
template<typename T>
class ComponentStorage {
public:
    void Add(Entity entity, const T& component);
    void Remove(Entity entity);
    T& Get(Entity entity);
    bool Has(Entity entity) const;
    auto begin() { return components_.begin(); }
    auto end() { return components_.end(); }
private:
    std::vector<T> components_;
    std::unordered_map<uint32_t, size_t> entityToIndex_;
};

// 系统基类
class System {
public:
    virtual ~System() = default;
    virtual void Update(float deltaTime, EntityManager& em) = 0;
    virtual void OnEntityCreated(Entity entity) {}
    virtual void OnEntityDestroyed(Entity entity) {}
    virtual std::vector<std::type_index> GetDependencies() const { return {}; }
};

// 前向声明
class EntityManager;
class SceneManager;

// 物理系统：写回 Scene Graph（通过 SceneNodeRef）
class PhysicsSystem : public System {
public:
    std::vector<std::type_index> GetDependencies() const override { return {}; }
    
    void Update(float deltaTime, EntityManager& em) override {
        auto* sceneMgr = em.GetSceneManager();
        if (!sceneMgr) return;
        
        for (auto& entity : em.EntitiesWith<PhysicsComponent, SceneNodeRef>()) {
            auto& physics = em.GetComponent<PhysicsComponent>(entity);
            auto& nodeRef = em.GetComponent<SceneNodeRef>(entity);
            auto* node = nodeRef.GetNode(sceneMgr);
            if (!node) continue;  // 节点已销毁，跳过
            
            physics.velocity += glm::vec3(0, -9.8f, 0) * deltaTime;
            physics.position += physics.velocity * deltaTime;
            
            glm::mat4 local = glm::translate(glm::mat4(1.0f), physics.position) *
                              glm::mat4_cast(physics.rotation);
            node->SetLocalTransform(local);
        }
    }
};

// 动画系统：依赖物理系统（若需先执行物理再写骨骼）
class AnimationSystem : public System {
public:
    std::vector<std::type_index> GetDependencies() const override {
        return {typeid(PhysicsSystem)};  // 依赖物理后执行
    }
    void Update(float deltaTime, EntityManager& em) override { /* ... */ }
};

// 实体管理器（构造时注入 RenderTaskScheduler* 和 SceneManager*）
class EntityManager {
public:
    explicit EntityManager(RenderTaskScheduler* scheduler, SceneManager* sceneMgr = nullptr);
    void SetSceneManager(SceneManager* sceneMgr);
    SceneManager* GetSceneManager() const;
    Entity CreateEntity();
    void DestroyEntity(Entity entity);
    bool IsAlive(Entity entity) const;
    void Update(float deltaTime);  // 根据 GetDependencies 构建 DAG 后并行执行
    
    template<typename T>
    void AddComponent(Entity entity, const T& component);
    template<typename T>
    T& GetComponent(Entity entity);
    template<typename... Components>
    auto EntitiesWith();  // 返回具有指定组件的实体迭代器
};

} // namespace RenderEngine
```

### 4. 渲染图系统（含显式提交）

```cpp
// RenderGraph.h - 依设计：应用层 CullScene 后 SubmitRenderable，Execute 时按 PassFlags 过滤绘制
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>

namespace RenderEngine {

class Renderable;
class IRenderDevice;

// 每帧提交的绘制项
struct SubmittedDraw {
    Renderable* renderable;
    glm::mat4 worldTransform;
    PassFlags passFlags;
};

// Pass 执行上下文
struct RenderPassContext {
    const std::vector<SubmittedDraw>& GetSubmittedDraws() const;
    std::vector<SubmittedDraw> GetDrawsForPass(PassFlags pass) const;
};

class RenderGraph {
public:
    struct ResourceDesc {
        std::string name;
        TextureDesc textureDesc;
        bool isExternal = false;
        TextureHandle externalTexture;
    };
    
    struct PassDesc {
        std::string name;
        std::vector<std::string> readTextures;
        std::vector<std::string> writeTextures;
        std::string depthTexture;
        std::function<void(const RenderPassContext&, CommandList&)> execute;
    };
    
    // 应用层显式提交可见 Renderable（从 CullScene 结果提交）
    void SubmitRenderable(Renderable* r, const glm::mat4& worldTransform, PassFlags passFlags = PassFlags::All);
    void ClearSubmitted();
    
    void DeclareTexture(const std::string& name, const TextureDesc& desc);
    void ImportTexture(const std::string& name, TextureHandle texture);
    void AddPass(const PassDesc& desc);
    
    void Compile(IRenderDevice* device);
    void Execute(IRenderDevice* device);  // WaitPrevFrameFence → AcquireImage → BuildFrameDrawList → Record → Submit

private:
    std::vector<SubmittedDraw> submittedDraws_;
    std::vector<PassDesc> passes_;
    std::unordered_map<std::string, ResourceDesc> resources_;
    std::unordered_map<std::string, TextureHandle> actualTextures_;
};

} // namespace RenderEngine
```

---

## 完整使用示例

### 示例 1: 简单的三角形渲染

```cpp
#include "RenderEngine.h"

int main() {
    using namespace RenderEngine;
    
    // 1. 初始化引擎
    RenderEngine::Config config;
    config.width = 1920;
    config.height = 1080;
    config.backend = RenderEngine::Config::Vulkan;
    
    RenderEngine engine;
    if (!engine.Initialize(config)) {
        return -1;
    }
    
    auto device = engine.GetRenderDevice();
    
    // 2. 创建顶点数据
    struct Vertex {
        glm::vec3 position;
        glm::vec3 color;
    };
    
    std::vector<Vertex> vertices = {
        {{0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.5f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    };
    
    BufferDesc bufferDesc;
    bufferDesc.size = sizeof(Vertex) * vertices.size();
    bufferDesc.usage = BufferUsage::Vertex;
    
    auto vertexBuffer = device->CreateBuffer(bufferDesc, vertices.data());
    
    // 3. 创建着色器
    auto vertShader = device->CreateShader({
        ShaderStage::Vertex,
        LoadSPIRV("shaders/triangle.vert.spv")
    });
    
    auto fragShader = device->CreateShader({
        ShaderStage::Fragment,
        LoadSPIRV("shaders/triangle.frag.spv")
    });
    
    // 4. 创建管线
    PipelineDesc pipelineDesc;
    pipelineDesc.shaders = {vertShader, fragShader};
    pipelineDesc.vertexBindings = {{0, sizeof(Vertex)}};
    pipelineDesc.vertexAttributes = {
        {0, 0, Format::RGB32F, offsetof(Vertex, position)},
        {1, 0, Format::RGB32F, offsetof(Vertex, color)},
    };
    pipelineDesc.colorAttachmentFormats = {Format::RGBA8_SRGB};
    
    auto pipeline = device->CreatePipeline(pipelineDesc);
    
    // 5. 主循环
    while (engine.IsRunning()) {
        engine.PollEvents();
        
        auto cmd = device->BeginCommandList();
        
        auto backBuffer = device->GetBackBuffer();
        cmd->BeginRenderPass({backBuffer}, {});
        
        cmd->SetViewport(0, 0, config.width, config.height);
        cmd->SetScissor(0, 0, config.width, config.height);
        
        cmd->BindPipeline(pipeline);
        cmd->BindVertexBuffer(0, vertexBuffer);
        cmd->Draw(3);
        
        cmd->EndRenderPass();
        
        device->EndCommandList(cmd);
        device->Submit({cmd});
        device->Present();
    }
    
    engine.Shutdown();
    return 0;
}
```

### 示例 2: 使用 Scene Graph + ECS 的完整场景（依设计）

```cpp
#include "RenderEngine.h"

// 依设计：Scene Graph 负责 Transform 与剔除，ECS 负责逻辑，应用层显式提交
class MyGame : public Application {
public:
    bool OnInitialize() override {
        auto* resMgr = engine->GetResourceManager();
        auto* sceneMgr = engine->GetSceneManager();
        auto* entityMgr = engine->GetEntityManager();
        
        // 1. 加载资源
        auto meshHandle = resMgr->Load<Mesh>("models/cube.gltf");
        auto matHandle = resMgr->Load<Material>("materials/brick.json");
        auto* mesh = resMgr->Get(meshHandle);
        auto* material = resMgr->Get(matHandle);
        
        // 2. 创建场景图（Transform 由 Scene Graph 管理）
        sceneRoot = sceneMgr->CreateScene();
        
        // 3. 创建多个渲染对象（挂载到 Scene Graph）
        for (int x = -2; x <= 2; ++x) {
            for (int z = -2; z <= 2; ++z) {
                auto node = CreateStaticMeshNode(mesh, material);
                node->SetLocalTransform(glm::translate(glm::mat4(1.0f), glm::vec3(x * 2.0f, 0, z * 2.0f)) *
                                       glm::scale(glm::mat4(1.0f), glm::vec3(0.5f)));
                sceneRoot->AddChild(std::move(node));
            }
        }
        
        // 4. 创建玩家控制的实体（ECS 逻辑 + SceneNodeRef 桥接）
        auto playerNode = CreateStaticMeshNode(mesh, material);
        playerNode->SetLocalTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0)));
        auto* playerSceneNode = sceneRoot->AddChild(std::move(playerNode));
        
        auto playerEntity = entityMgr->CreateEntity();
        entityMgr->AddComponent(playerEntity, PhysicsComponent{});
        entityMgr->AddComponent(playerEntity, PlayerController{});
        entityMgr->AddComponent(playerEntity, SceneNodeRef::FromNode(playerSceneNode));  // 安全引用
        
        // 5. 设置相机（Camera 作为 Scene Graph 节点）
        auto cameraNode = std::make_unique<CameraNode>();
        cameraNode->SetLocalTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 5)));
        camera = static_cast<CameraNode*>(sceneRoot->AddChild(std::move(cameraNode)));
        
        // 6. 输入绑定
        auto* input = engine->GetInputManager();
        input->AddActionBinding("MoveForward", Keyboard(KeyCode::W));
        input->AddActionBinding("MoveBack", Keyboard(KeyCode::S));
        input->AddActionBinding("MoveLeft", Keyboard(KeyCode::A));
        input->AddActionBinding("MoveRight", Keyboard(KeyCode::D));
        
        // 7. 设置渲染图（使用 engine 的 RenderGraph）
        SetupRenderGraph();
        
        return true;
    }
    
    void OnUpdate(float deltaTime) override {
        // ECS 与 Scene 的 Update 由 Engine::Run 统一调用
        // 此处仅做应用层逻辑（UI、游戏状态等）
    }
    
    void OnRender() override {
        // 依设计：CullScene → SubmitRenderable → Execute
        auto visibleNodes = engine->GetSceneManager()->CullScene(camera);
        auto* rg = engine->GetRenderGraph();
        rg->ClearSubmitted();
        for (auto* node : visibleNodes) {
            if (auto* r = node->GetRenderable())
                rg->SubmitRenderable(r, node->GetWorldMatrix(), node->GetPassFlags());
        }
        engine->GetRenderGraph()->Execute(engine->GetRenderDevice());
    }
    
private:
    void SetupRenderGraph() {
        auto* rg = engine->GetRenderGraph();
        
        rg->DeclareTexture("ShadowMap", {2048, 2048, 1, 1, 1, Format::D32, TextureUsage::DepthAttachment});
        rg->DeclareTexture("GBuffer.Albedo", {1920, 1080, 1, 1, 1, Format::RGBA8_SRGB, TextureUsage::ColorAttachment});
        rg->DeclareTexture("GBuffer.Normal", {1920, 1080, 1, 1, 1, Format::RGBA16F, TextureUsage::ColorAttachment});
        rg->DeclareTexture("Depth", {1920, 1080, 1, 1, 1, Format::D24S8, TextureUsage::DepthAttachment});
        rg->DeclareTexture("LightingResult", {1920, 1080, 1, 1, 1, Format::RGBA16F, TextureUsage::ColorAttachment});
        
        rg->AddPass({
            .name = "ShadowPass",
            .depthTexture = "ShadowMap",
            .execute = [](const RenderPassContext& ctx, CommandList& cmd) {
                for (const auto& draw : ctx.GetDrawsForPass(PassFlags::ShadowCaster)) {
                    draw.renderable->Draw(cmd, draw.worldTransform);
                }
            }
        });
        
        rg->AddPass({
            .name = "GBufferPass",
            .readTextures = {"ShadowMap"},
            .writeTextures = {"GBuffer.Albedo", "GBuffer.Normal"},
            .depthTexture = "Depth",
            .execute = [](const RenderPassContext& ctx, CommandList& cmd) {
                for (const auto& draw : ctx.GetDrawsForPass(PassFlags::Opaque)) {
                    draw.renderable->Draw(cmd, draw.worldTransform);
                }
            }
        });
        
        rg->AddPass({
            .name = "LightingPass",
            .readTextures = {"GBuffer.Albedo", "GBuffer.Normal", "Depth", "ShadowMap"},
            .writeTextures = {"LightingResult"},
            .execute = [](const RenderPassContext& ctx, CommandList& cmd) { /* PBR 光照 */ }
        });
        
        rg->ImportTexture("BackBuffer", engine->GetRenderDevice()->GetBackBuffer());
        rg->AddPass({
            .name = "PostProcess",
            .readTextures = {"LightingResult"},
            .writeTextures = {"BackBuffer"},
            .execute = [](const RenderPassContext& ctx, CommandList& cmd) { /* Tone Mapping, FXAA */ }
        });
        
        rg->Compile(engine->GetRenderDevice());
    }
    
    SceneNode* sceneRoot = nullptr;
    CameraNode* camera = nullptr;
};

int main() {
    RenderEngine engine;
    if (!engine.Initialize({})) return -1;
    MyGame app;
    engine.Run(&app);
    engine.Shutdown();
    return 0;
}
```

---

## 性能优化技巧

### 1. GPU Instancing

```cpp
// 批量渲染相同Mesh的多个实例
struct InstanceData {
    glm::mat4 modelMatrix;
    glm::vec4 color;
};

void RenderInstanced(CommandList& cmd, Mesh* mesh, const std::vector<InstanceData>& instances) {
    // 创建Instance Buffer
    BufferDesc bufferDesc;
    bufferDesc.size = sizeof(InstanceData) * instances.size();
    bufferDesc.usage = BufferUsage::Vertex;
    auto instanceBuffer = device->CreateBuffer(bufferDesc, instances.data());
    
    cmd->BindVertexBuffer(0, mesh->GetVertexBuffer());
    cmd->BindVertexBuffer(1, instanceBuffer); // Instance data
    cmd->BindIndexBuffer(mesh->GetIndexBuffer());
    cmd->DrawIndexed(mesh->GetIndexCount(), instances.size());
}
```

### 2. 多线程命令录制

```cpp
void RenderSceneMultithreaded(const std::vector<RenderBatch>& batches) {
    std::vector<CommandList*> commandLists;
    
    // 使用 executor 并行录制
    std::vector<TaskHandle> tasks;
    
    for (size_t i = 0; i < batches.size(); ++i) {
        tasks.push_back(scheduler->SubmitRenderTask([&, i]() {
            auto cmd = device->BeginCommandList();
            
            for (const auto& drawCall : batches[i].drawCalls) {
                cmd->BindPipeline(drawCall.pipeline);
                cmd->BindVertexBuffer(0, drawCall.vertexBuffer);
                cmd->BindIndexBuffer(drawCall.indexBuffer);
                cmd->DrawIndexed(drawCall.indexCount);
            }
            
            device->EndCommandList(cmd);
            return cmd;
        }));
    }
    
    // 等待所有任务完成
    for (auto& task : tasks) {
        commandLists.push_back(task.Get());
    }
    
    // 提交所有命令列表
    device->Submit(commandLists);
}
```

### 3. 资源流式加载

```cpp
class StreamingResourceManager {
public:
    Future<Texture*> LoadTextureAsync(const std::string& path, int priority = 0) {
        return scheduler->SubmitTask([this, path]() {
            // 1. 从磁盘加载
            auto imageData = LoadImageFromDisk(path);
            
            // 2. 创建Staging Buffer
            auto stagingBuffer = device->CreateBuffer({
                .size = imageData.size,
                .usage = BufferUsage::Transfer,
                .cpuVisible = true
            }, imageData.pixels);
            
            // 3. 创建GPU Texture
            auto texture = device->CreateTexture({
                .width = imageData.width,
                .height = imageData.height,
                .format = imageData.format,
                .usage = TextureUsage::Sampled | TextureUsage::Transfer
            });
            
            // 4. 异步上传
            auto cmd = device->BeginCommandList();
            cmd->CopyBufferToTexture(stagingBuffer, texture);
            device->EndCommandList(cmd);
            device->Submit({cmd});
            
            // 5. 清理Staging Buffer (等GPU完成后)
            auto fence = device->CreateFence();
            device->WaitForFence(fence);
            device->DestroyBuffer(stagingBuffer);
            
            return texture;
        }, priority);
    }
};
```

### 4. 剔除优化（SceneManager::CullScene 内部示意）

```cpp
// 依设计：剔除内含于 SceneManager，CullScene 返回可见 SceneNode 列表
std::vector<SceneNode*> SceneManager::CullScene(CameraNode* camera) {
    std::vector<SceneNode*> visibleNodes;
    auto frustum = ExtractFrustumPlanes(camera->projectionMatrix * camera->viewMatrix);
    // 注：SceneManager 为 SceneNode 的 friend，可访问 node->children
    
    // 递归遍历场景图，视锥剔除
    std::function<void(SceneNode*)> cullRecursive = [&](SceneNode* node) {
        if (!node->GetRenderable()) {
            for (auto& child : node->children)
                cullRecursive(child.get());
            return;
        }
        
        auto worldBounds = TransformBounds(node->GetRenderable()->GetBounds(), node->GetWorldMatrix());
        if (!IsBoundsInFrustum(worldBounds, frustum)) return;
        
        // 可选：LOD 选择（LOD Manager 选择后写入 Renderable/SceneNode）
        // if (lodManager_) lodManager_->SelectLOD(node, camera);
        
        visibleNodes.push_back(node);
        
        for (auto& child : node->children)
            cullRecursive(child.get());
    };
    
    cullRecursive(activeRoot_);
    
    // 可选：遮挡剔除（Phase 6）
    // if (enableOcclusionCulling_) OcclusionCull(visibleNodes, hiZBuffer_);
    
    return visibleNodes;
}
```

---

## 常见问题和解决方案

### Q1: 场景切换时如何避免 SceneNodeRef 悬空引用?

```cpp
// 依设计：先解绑旧场景的 SceneNodeRef，再调用 SetActiveScene
void SwitchToNewLevel(SceneNode* newSceneRoot) {
    auto* entityMgr = engine->GetEntityManager();
    auto* sceneMgr = engine->GetSceneManager();
    
    // 1. 遍历所有持有 SceneNodeRef 的 Entity，若其 handle 指向旧场景则解绑
    for (auto& entity : entityMgr->EntitiesWith<SceneNodeRef>()) {
        auto& ref = entityMgr->GetComponent<SceneNodeRef>(entity);
        auto* node = ref.GetNode(sceneMgr);
        if (node && IsDescendantOf(currentSceneRoot_, node)) {
            entityMgr->RemoveComponent<SceneNodeRef>(entity);
        }
    }
    
    // 2. 切换场景
    sceneMgr->SetActiveScene(newSceneRoot);
    currentSceneRoot_ = newSceneRoot;
    
    // 3. 为新场景中的需要逻辑控制的节点重新绑定 SceneNodeRef
    // ...
}
```

### Q2: 如何在 Vulkan 和 OpenGL 之间无缝切换?

```cpp
// 使用抽象工厂模式
class RenderDeviceFactory {
public:
    static std::unique_ptr<IRenderDevice> Create(Backend backend) {
        switch (backend) {
            case Backend::Vulkan:
                return std::make_unique<VulkanRenderDevice>();
            case Backend::OpenGL:
                return std::make_unique<OpenGLRenderDevice>();
            default:
                return nullptr;
        }
    }
};

// 使用时
auto device = RenderDeviceFactory::Create(config.backend);
device->Initialize(config);
```

### Q3: 如何处理着色器的跨平台编译?

```cpp
class ShaderCompiler {
public:
    std::vector<uint32_t> CompileToSPIRV(const std::string& source, ShaderStage stage) {
        // 使用 glslang 或 shaderc 编译为 SPIR-V
        // SPIR-V 可以在 Vulkan 中直接使用
        // 也可以通过 SPIRV-Cross 转换为 GLSL
    }
    
    std::string ConvertToGLSL(const std::vector<uint32_t>& spirv, int glslVersion = 450) {
        // 使用 SPIRV-Cross 转换
        spirv_cross::CompilerGLSL glsl(spirv);
        return glsl.compile();
    }
};
```

### Q4: 如何实现热重载?

```cpp
class HotReloadManager {
public:
    void Watch(const std::string& path, std::function<void()> callback) {
        // 使用文件监控 (inotify/kqueue/FileSystemWatcher)
        watchers[path] = {
            .lastModified = GetFileModificationTime(path),
            .callback = callback
        };
    }
    
    void Update() {
        for (auto& [path, watcher] : watchers) {
            auto currentTime = GetFileModificationTime(path);
            if (currentTime > watcher.lastModified) {
                watcher.lastModified = currentTime;
                watcher.callback();
            }
        }
    }
};

// 使用示例
hotReload->Watch("shaders/pbr.frag", [this]() {
    auto newShader = CompileShader("shaders/pbr.frag");
    device->DestroyShader(oldShader);
    oldShader = newShader;
    // 重新创建管线...
});
```

### Q5: 如何优化内存使用?

```cpp
// 使用对象池
template<typename T>
class ObjectPool {
public:
    T* Allocate() {
        if (!freeList.empty()) {
            T* obj = freeList.back();
            freeList.pop_back();
            return obj;
        }
        return new T();
    }
    
    void Free(T* obj) {
        freeList.push_back(obj);
    }
    
private:
    std::vector<T*> freeList;
};

// 使用 VMA 进行高效 GPU 内存分配
class VulkanMemoryManager {
public:
    void Initialize(VkDevice device, VkPhysicalDevice physicalDevice) {
        VmaAllocatorCreateInfo createInfo = {};
        createInfo.device = device;
        createInfo.physicalDevice = physicalDevice;
        vmaCreateAllocator(&createInfo, &allocator);
    }
    
    VkBuffer CreateBuffer(const VkBufferCreateInfo& bufferInfo, VmaMemoryUsage usage) {
        VmaAllocationCreateInfo allocInfo = {};
        allocInfo.usage = usage;
        
        VkBuffer buffer;
        VmaAllocation allocation;
        vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr);
        
        return buffer;
    }
    
private:
    VmaAllocator allocator;
};
```

---

这个补充文档提供了核心类的详细实现和实用示例。结合主设计文档,你应该能够开始构建这个渲染引擎了!
