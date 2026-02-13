# 设备抽象层 (Device Abstraction Layer) 实现任务清单

> 基于 [device_abstraction_layer_design.md](../design/device_abstraction_layer_design.md)、[rendering_engine_design.md](../design/rendering_engine_design.md)、[rendering_engine_code_examples.md](../design/rendering_engine_code_examples.md) 设计文档构造。

## 设计目标

- **后端可替换**：RDI 支持 Vulkan、OpenGL 等后端，可在运行时或编译时切换
- **平台无关**：通过 SDL3 抽象窗口与输入，实现跨平台（Windows、Linux、macOS）
- **与渲染管线适配**：支持多线程命令录制、帧流水线（Fence/Semaphore）、Render Graph 依赖
- **资源生命周期清晰**：句柄化资源管理，避免跨层悬空引用
- **高性能**：避免不必要的抽象开销，关键路径直接映射到原生 API

---

## Phase 1：基础框架（2–3 周）

### 1.1 SDL3 与窗口系统

- [x] 集成 SDL3 库（`SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS`）
- [x] 实现 `WindowSystem` 类
- [x] 实现 `WindowSystem::Create(const WindowConfig& config)` 创建 SDL 窗口
- [x] 实现 `WindowSystem::Destroy()` 销毁窗口
- [x] 实现 `WindowSystem::GetWindow()` 返回 `SDL_Window*`
- [x] 实现 `WindowSystem::GetNativeHandle()` 供 Vulkan Surface / OpenGL Context 使用
- [x] 实现 `GetWidth()` / `GetHeight()` / `Resize()`
- [x] 实现 `PollEvents()` 与 `ShouldClose()` 事件循环
- [x] 定义 `WindowConfig` 结构（width, height, title, fullscreen, resizable）

### 1.2 Vulkan 基础

- [x] 创建 Vulkan Instance（支持 `enableValidation` 时启用 Validation Layer）
- [x] 选择 Physical Device 与 Queue Family
- [x] 创建 Logical Device
- [x] 使用 `SDL_Vulkan_CreateSurface(window, instance, &surface)` 创建 Surface
- [x] 创建基础 Swapchain（支持 vsync、backBufferCount）
- [x] 获取 Swapchain Image 句柄

### 1.3 简单三角形渲染

- [x] 创建 Vulkan Command Pool 与 Command Buffer
- [x] 实现 Render Pass（单次 Color Attachment）
- [x] 创建三角形顶点 Buffer（Vertex + Index）
- [x] 加载 SPIR-V 着色器，创建 Shader Module
- [x] 创建 Graphics Pipeline（TriangleList）
- [x] 实现一帧完整流程：Acquire → Record → Submit → Present

### 1.4 输入系统基础

- [x] 实现 `InputManager` 类
- [x] 实现 `InputManager::Initialize(SDL_Window* window)`
- [x] 实现 `InputManager::Update()` 每帧轮询 SDL 事件
- [x] 实现键盘：`IsKeyPressed(KeyCode)` / `IsKeyJustPressed` / `IsKeyJustReleased`
- [x] 实现鼠标：`GetMousePosition()` / `GetMouseDelta()` / `IsMouseButtonPressed()` / `GetMouseWheelDelta()`
- [x] 定义 `KeyCode`、`MouseButton` 枚举（与 SDL 映射）

---

## Phase 2：RDI 完整实现（2–3 周）

### 2.1 资源句柄与描述符类型

- [x] 实现 `Handle<T>` 模板（id, IsValid, operator==, operator!=）
- [x] 定义 `BufferHandle`、`TextureHandle`、`ShaderHandle`、`PipelineHandle`
- [x] 定义 `DescriptorSetHandle`、`FenceHandle`、`SemaphoreHandle`
- [x] 定义 `Format`、`BufferUsage`、`TextureUsage` 枚举
- [x] 定义 `BufferDesc`、`TextureDesc`、`ShaderDesc` 结构
- [x] 定义 `DescriptorBinding`、`DescriptorSetLayoutDesc`（含 DescriptorType）
- [x] 定义 `VertexInputBinding`、`VertexInputAttribute`
- [x] 定义 `BlendState`、`DepthStencilState`、`RasterizationState`
- [x] 定义 `PipelineDesc`（shaders, vertexBindings, vertexAttributes, topology, rasterization, depthStencil, blendStates, colorAttachmentFormats, depthAttachmentFormat）

### 2.2 IRenderDevice 接口定义

- [x] 实现 `IRenderDevice` 纯虚基类
- [x] 实现 `Initialize(const DeviceConfig& config)` / `Shutdown()`
- [x] 实现资源创建：`CreateBuffer`、`CreateTexture`、`CreateShader`、`CreatePipeline`、`CreateDescriptorSet`
- [x] 实现资源销毁：`DestroyBuffer`、`DestroyTexture`、`DestroyShader`、`DestroyPipeline`、`DestroyDescriptorSet`
- [x] 实现资源更新：`UpdateBuffer`、`UpdateTexture`
- [x] 实现 `BeginCommandList(uint32_t threadIndex)` / `EndCommandList` / `Submit`
- [x] 实现同步：`WaitIdle`、`CreateFence`、`WaitForFence`、`ResetFence`、`CreateSemaphore`
- [x] 实现交换链：`AcquireNextImage`、`Present`、`GetBackBuffer`、`GetCurrentFrameIndex`
- [x] 实现 `GetCapabilities()` 返回 `DeviceCapabilities`
- [x] 定义 `DeviceConfig`、`DeviceCapabilities` 结构
- [x] 实现 `GetLastError()` 错误信息（初始化失败时）
- [x] 实现 `CreateRenderDevice(Backend backend)` 工厂函数

### 2.3 CommandList 接口

- [x] 实现 `CommandList` 纯虚接口
- [x] 实现 Render Pass：`BeginRenderPass`、`EndRenderPass`
- [x] 实现管线绑定：`BindPipeline`、`BindDescriptorSet`
- [x] 实现资源绑定：`BindVertexBuffer`、`BindIndexBuffer`
- [x] 实现 `SetPushConstants`
- [x] 实现 Draw：`Draw`、`DrawIndexed`
- [x] 实现 Compute：`Dispatch`
- [x] 实现 `Barrier`（纹理资源屏障）
- [x] 实现 Clear / Viewport / Scissor：`ClearColor`、`ClearDepth`、`SetViewport`、`SetScissor`

### 2.4 Vulkan Backend 资源

- [x] 实现 `VulkanRenderDevice : public IRenderDevice`
- [x] 实现 `CreateBuffer` → VkBuffer + VMA 分配
- [x] 实现 `CreateTexture` → VkImage + VMA 分配
- [x] 实现 `CreateShader` → VkShaderModule（SPIR-V）
- [x] 实现 `CreatePipeline` → VkPipeline（含 layout）
- [x] 实现 `CreateDescriptorSet` → VkDescriptorSet / VkDescriptorPool
- [x] 实现 `Destroy*` 系列接口
- [x] 实现 `UpdateBuffer` / `UpdateTexture`（含 Staging Buffer 上传路径）

### 2.5 Vulkan Backend 命令与同步

- [x] 实现 `VulkanCommandList` 封装 VkCommandBuffer
- [x] 实现 `BeginCommandList(threadIndex)` 从对应 CommandPool 分配
- [x] 实现 `EndCommandList` 结束录制
- [x] 实现 `Submit` 支持 waitSemaphores、signalSemaphores、fence
- [x] 实现 Fence：`CreateFence`、`WaitForFence`、`ResetFence`
- [x] 实现 Semaphore：`CreateSemaphore`
- [x] 实现帧流水线：Acquire 时 signal semaphore，Submit 时 wait/signal
- [x] 集成 VMA (Vulkan Memory Allocator)

### 2.6 Vulkan Swapchain 与呈现

- [x] 实现 `AcquireNextImage` 返回 back buffer index，内部 signal semaphore
- [x] 实现 `Present` 与 Swapchain 呈现
- [x] 实现 `GetBackBuffer` 返回当前帧 TextureHandle
- [x] 实现 `GetCurrentFrameIndex` 帧索引
- [x] 处理 `VK_ERROR_OUT_OF_DATE_KHR`（resize 时重建 Swapchain）

---

## Phase 3：OpenGL 后端（1–2 周）

### 3.1 OpenGL 基础

- [x] 实现 `OpenGLRenderDevice : public IRenderDevice`
- [x] 使用 `SDL_GL_CreateContext(window)` 创建 GL Context
- [x] 实现 `Initialize` / `Shutdown`
- [x] 实现 Swapchain 语义（OpenGL 由窗口系统隐式提供）
- [x] 实现 `AcquireNextImage` / `Present` / `GetBackBuffer`（GL 适配）

### 3.2 命令队列与 GL 映射

- [x] 设计 `GLCommand` 结构表示命令（类型 + 参数）
- [x] 实现 `OpenGLCommandList` 将 CommandList 调用序列化为 GLCommand 队列
- [x] 实现 `BeginCommandList(threadIndex)` 分配可录制对象（threadIndex 忽略）
- [x] 实现 `EndCommandList` 缓存命令
- [x] 实现 `Submit` 按序执行 GL 调用（glDraw*、glBind* 等）
- [x] 实现资源创建：Buffer → GL Buffer，Texture → GL Texture，Pipeline → GL Program
- [x] 实现 DescriptorSet 映射到纹理单元

### 3.3 状态缓存

- [x] 实现 OpenGL 状态缓存（减少冗余 glBindTexture、glUseProgram 等）
- [x] 仅在状态变化时调用 GL API
- [x] 缓存当前 bound pipeline、descriptor sets、vertex/index buffers

### 3.4 同步与多线程

- [x] 实现 Fence：`glFenceSync` / `glClientWaitSync`
- [x] 实现 Semaphore：OpenGL 无原生语义，可用 Fence 模拟或简化
- [x] 多线程录制：`ParallelRecordCommands` 时 OpenGL 退化为串行

---

## Phase 4：窗口与输入完善（1 周）

### 4.1 窗口 Resize 与 Swapchain

- [x] 实现 `WindowSystem::Resize()` 通知尺寸变化
- [x] Vulkan：检测 `VK_ERROR_OUT_OF_DATE_KHR`，重建 Swapchain
- [x] 在 `AcquireNextImage` 或 `Present` 时检测并处理
- [x] 最小化时：可暂停 Present 或使用空帧

### 4.2 手柄支持

- [x] 实现 `IsGamepadConnected(int index)`
- [x] 实现 `GetGamepadAxis(int index, GamepadAxis axis)`
- [x] 实现 `IsGamepadButtonPressed(int index, GamepadButton button)`
- [x] 定义 `GamepadAxis`、`GamepadButton` 枚举
- [x] 支持手柄热插拔（SDL3 事件）

### 4.3 Action Mapping

- [x] 定义 `InputBinding = std::variant<KeyCode, MouseButton, GamepadBinding>`
- [x] 定义 `GamepadBinding` 结构（gamepadIndex + input）
- [x] 实现 `AddActionBinding(const std::string& action, const InputBinding& binding)`
- [x] 实现 `ClearActionBindings(const std::string& action)`
- [x] 实现 `IsActionTriggered(const std::string& action)`
- [x] 实现 `GetActionValue(const std::string& action)`（轴值）
- [x] 实现便捷构造：`Keyboard()`、`Mouse()`、`GamepadButtonBinding()`、`GamepadAxisBinding()`
- [x] 同一 action 支持多绑定（如 W 与上箭头）

### 4.4 输入事件回调

- [x] 定义 `InputEventType` 枚举
- [x] 定义 `InputEvent` 结构
- [x] 实现 `RegisterCallback(InputEventType type, std::function<void(const InputEvent&)> callback)`
- [x] 在 `Update()` 中派发事件

### 4.5 输入状态双缓冲

- [x] 维护当前帧与上一帧状态
- [x] 正确实现 `JustPressed` / `JustReleased` 判断

---

## Phase 5：多线程与优化（1–2 周）

### 5.1 Vulkan 多线程 CommandPool

- [x] 预分配每线程独立 CommandPool（`std::vector<VkCommandPool> commandPools_`）
- [x] `BeginCommandList(threadIndex)` 从 `commandPools_[threadIndex]` 分配
- [x] Submit 时按拓扑序合并多个 CommandList
- [x] 与 `RenderTaskScheduler::ParallelRecordCommands` 集成
- [x] 验证 `ParallelRecordCommands` 时每线程独立录制无竞争

### 5.2 DescriptorSet 池化

- [x] 实现 DescriptorSet 池（按 layout 分组）
- [x] `AcquireInstanceDescriptorSet` 从池分配
- [x] 帧末 `ReleaseAllInstanceDescriptorSets` 回收
- [x] 与 Material 实例级 DescriptorSet 生命周期对齐

### 5.3 帧流水线完善

- [x] 实现完整帧流水线：WaitFence → ResetFence → Acquire → Record → Submit(wait, signal, fence) → Present
- [x] 与 Render Graph `Execute` 流程对齐
- [x] 支持 `kMaxFramesInFlight`（如 3）帧并发

### 5.4 设备能力查询

- [x] 实现 `DeviceCapabilities` 完整填充（maxTextureSize, maxComputeWorkGroupSize, supportsGeometryShader 等）
- [x] Vulkan：从 VkPhysicalDevice 查询
- [ ] OpenGL：从 GL 扩展查询

### 5.5 性能测试与调优

- [x] 性能基准：三角形渲染、多 DrawCall、多线程录制
- [x] 验证无每帧 WaitIdle，仅使用 Fence 同步
- [x] 调优：减少 GL 状态切换、Vulkan 命令缓冲复用

---

## 与上层集成

### RenderEngine 初始化顺序

- [x] `SDL_Init()` → `WindowSystem::Create()` → `CreateRenderDevice()` → `InputManager::Initialize()`
- [x] `DeviceConfig` 从 `windowSystem_->GetNativeHandle()` 传入
- [x] 实现 `RenderEngine::Initialize` 中设备抽象层初始化流程

### 主循环

- [x] `Run()` 中：`inputManager->Update()` → `PollEvents` → `OnUpdate` → `OnRender` → `Present`
- [x] Present 在 Execute 之后由 Run 调用

### 依赖关系

| 上层模块 | 依赖设备抽象层 |
|----------|----------------|
| Render Graph | IRenderDevice |
| Resource Manager | IRenderDevice（Buffer/Texture 创建） |
| Staging Memory Manager | IRenderDevice（Upload Queue） |
| Application | InputManager, WindowSystem |

---

## 错误处理与生命周期

### 初始化失败

- [x] `IRenderDevice::Initialize()` 返回 `false` 时，`GetLastError()` 返回详细原因
- [x] 调用方不继续调用其他接口，必要时调用 `Shutdown()` 清理

### 资源生命周期

- [x] 句柄无效：`Handle::IsValid()` 返回 false 时，调用方不得使用
- [x] 销毁顺序：先销毁依赖资源的资源（如 Pipeline 依赖 Shader），再销毁底层资源

### 窗口与 Swapchain 重建

- [x] 窗口 resize 时正确处理 `VK_ERROR_OUT_OF_DATE_KHR`
- [x] 最小化时策略（暂停 Present 或空帧）

---

## 参考文档

- [device_abstraction_layer_design.md](../design/device_abstraction_layer_design.md) - 设备抽象层完整设计
- [rendering_engine_design.md](../design/rendering_engine_design.md) - 渲染引擎架构
- [rendering_engine_code_examples.md](../design/rendering_engine_code_examples.md) - 代码示例
- [executor_layer_design.md](../design/executor_layer_design.md) - 多线程命令录制协作
