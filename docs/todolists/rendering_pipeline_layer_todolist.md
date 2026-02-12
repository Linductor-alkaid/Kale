# 渲染管线层 (Rendering Pipeline Layer) 实现任务清单

> 基于 [rendering_pipeline_layer_design.md](../design/rendering_pipeline_layer_design.md)、[rendering_engine_design.md](../design/rendering_engine_design.md)、[rendering_engine_code_examples.md](../design/rendering_engine_code_examples.md) 设计文档构造。

## 设计目标

- **声明式管线**：通过 AddPass、DeclareTexture 等声明依赖，Compile 时自动分析并分配资源
- **显式提交**：应用层 CullScene 后 SubmitRenderable，RG 不主动拉取场景数据
- **Pass DAG**：Pass 间依赖构成 DAG，无依赖的 Pass 可并行录制
- **帧流水线**：WaitPrevFrameFence → Acquire → Record → Submit，Present 由 Run 在 Execute 之后调用
- **材质双级**：材质级 DescriptorSet 共享，实例级池化并于帧末回收

---

## Phase 1：Render Graph 基础（1–2 周）

### 1.1 SubmittedDraw 与 RenderPassContext

- [ ] 定义 `SubmittedDraw` 结构
- [ ] 实现 `renderable` 成员（`Renderable*`）
- [ ] 实现 `worldTransform` 成员（`glm::mat4`）
- [ ] 实现 `passFlags` 成员（`PassFlags`：ShadowCaster | Opaque | Transparent）
- [ ] 定义 `RenderPassContext` 结构
- [ ] 实现 `GetSubmittedDraws()` 返回 `const std::vector<SubmittedDraw>&`
- [ ] 实现 `GetDrawsForPass(PassFlags pass)` 按 Pass 过滤（过滤条件 `(draw.passFlags & pass) != 0`）

### 1.2 PassFlags 与 RG 资源句柄

- [ ] 定义 `PassFlags` 枚举（ShadowCaster、Opaque、Transparent、All）
- [ ] 定义 `RGResourceHandle = uint64_t` 类型（RG 内部逻辑句柄）
- [ ] Compile 时 RGResourceHandle 映射为 RDI 的 TextureHandle/BufferHandle

### 1.3 RenderPassBuilder

- [ ] 实现 `RenderPassBuilder` 类
- [ ] 实现 `WriteColor(uint32_t slot, RGResourceHandle texture)`
- [ ] 实现 `WriteDepth(RGResourceHandle texture)`
- [ ] 实现 `ReadTexture(RGResourceHandle texture)`
- [ ] 实现 `WriteSwapchain()` 声明写入当前 back buffer
- [ ] Setup 回调中通过 RenderPassBuilder 声明 Pass 读/写依赖

### 1.4 RenderGraph 声明式接口

- [ ] 实现 `RenderGraph` 类
- [ ] 实现 `DeclareTexture(const std::string& name, const TextureDesc& desc)` 返回 RGResourceHandle
- [ ] 实现 `DeclareBuffer(const std::string& name, const BufferDesc& desc)` 返回 RGResourceHandle
- [ ] 实现 `AddPass(const std::string& name, RenderPassSetup setup, RenderPassExecute execute)` 返回 RenderPassHandle
- [ ] 定义 `RenderPassSetup = std::function<void(RenderPassBuilder&)>` 类型
- [ ] 定义 `RenderPassExecute = std::function<void(const RenderPassContext&, CommandList&)>` 类型
- [ ] 实现 `SetResolution(uint32_t width, uint32_t height)` 影响 DeclareTexture 尺寸

### 1.5 应用层显式提交

- [ ] 实现 `SubmitRenderable(Renderable* renderable, const glm::mat4& worldTransform, PassFlags passFlags = PassFlags::All)`
- [ ] 实现 `ClearSubmitted()` 清空本帧提交
- [ ] 实现 `submittedDraws_` 成员存储每帧提交的绘制项

### 1.6 Compile 流程

- [ ] 实现 `Compile(IRenderDevice* device)` 在分辨率/管线变化时调用
- [ ] 实现 Pass 依赖分析（ReadTexture/WriteTexture 推导 Pass 顺序）
- [ ] 实现资源分配：按 DeclareTexture 描述创建 RDI Texture
- [ ] 实现 RGResourceHandle → 实际 RDI TextureHandle/BufferHandle 的映射
- [ ] 构建 `topologicalOrder_` Pass 拓扑序
- [ ] Compile 失败时返回 false 或抛出，GetLastError 获取原因
- [ ] 资源分配失败时保证已分配资源可释放

### 1.7 Execute 流程（单线程）

- [ ] 实现 `Execute(IRenderDevice* device)` 每帧调用
- [ ] 实现 `BuildFrameDrawList()` 整理 submittedDraws_
- [ ] 实现 帧流水线：`WaitForFence(frameFences_[currentFrameIndex_])`
- [ ] 实现 `ResetFence(frameFences_[currentFrameIndex_])`
- [ ] 实现 `AcquireNextImage()` 获取 imageIndex
- [ ] 实现 `RecordPasses(device)` 按拓扑序单线程录制
- [ ] 实现 `Submit(cmdLists, waitSem, signalSem, fence)`
- [ ] 实现 `ReleaseFrameResources()` 帧末回收
- [ ] 实现 `currentFrameIndex_` 轮转（`kMaxFramesInFlight = 3`）
- [ ] 实现 `frameFences_`、`acquireSemaphore_`、`renderCompleteSemaphore_` 管理

### 1.8 简单 Forward Pass

- [ ] 实现单 Pass 的 Forward 渲染（WriteSwapchain 直接绘制到 back buffer）
- [ ] Setup 示例：DeclareTexture + AddPass 单一 Forward Pass
- [ ] Execute 中 GetDrawsForPass(PassFlags::All) 绘制所有提交对象

---

## Phase 2：材质系统（1–2 周）

### 2.1 Material 基类

- [ ] 实现 `Material` 基类
- [ ] 实现 `SetTexture(const std::string& name, Texture* texture)`
- [ ] 实现 `SetParameter(const std::string& name, const void* data, size_t size)`
- [ ] 实现 `GetShader()` 返回 Shader*
- [ ] 实现 `GetPipeline()` 返回 PipelineHandle
- [ ] 实现 `parameters_` 存储材质参数（`std::unordered_map<std::string, MaterialParameter>`）

### 2.2 材质级 DescriptorSet

- [ ] 实现 `GetMaterialDescriptorSet()` 返回 DescriptorSetHandle
- [ ] 材质级 DescriptorSet 同一材质所有实例共享
- [ ] 包含纹理、采样器等不变资源
- [ ] 材质创建时分配并绑定纹理

### 2.3 实例级 DescriptorSet 池化

- [ ] 实现 `AcquireInstanceDescriptorSet(const void* instanceData, size_t size)` 返回 DescriptorSetHandle
- [ ] 实例级 DescriptorSet 用于 per-instance 数据（如 worldTransform）
- [ ] 实现池化复用，避免每帧大量分配
- [ ] 实现 `instanceDescriptorPool_` 或类似池化结构
- [ ] Acquire 在 Draw 时调用；Release 由 RenderGraph::ReleaseFrameResources 统一回收

### 2.4 ReleaseFrameResources

- [ ] 实现 `Material::ReleaseAllInstanceDescriptorSets()` 回收本帧分配的实例级 DescriptorSet
- [ ] 实现 `RenderGraph::ReleaseFrameResources()` 遍历本帧 SubmittedDraws 中的 Material
- [ ] Execute 结束时调用 ReleaseFrameResources
- [ ] 池化回收的 DescriptorSet 供下一帧复用

### 2.5 PBRMaterial

- [ ] 实现 `PBRMaterial : public Material`
- [ ] 实现 `SetAlbedo(Texture* tex)`
- [ ] 实现 `SetNormal(Texture* tex)`
- [ ] 实现 `SetMetallic(float value)`
- [ ] 实现 `SetRoughness(float value)`
- [ ] 实现 `SetAO(Texture* tex)`
- [ ] 实现 `SetEmissive(Texture* tex)`

### 2.6 Renderable::Draw 与 Material 绑定

- [ ] 实现 `StaticMesh::Draw(CommandList& cmd, const glm::mat4& worldTransform)`
- [ ] Draw 中：`cmd.BindPipeline(material_->GetPipeline())`
- [ ] Draw 中：`cmd.BindDescriptorSet(0, material_->GetMaterialDescriptorSet())`
- [ ] Draw 中：`material_->AcquireInstanceDescriptorSet(&worldTransform, sizeof(glm::mat4))` 并 BindDescriptorSet(1)
- [ ] Draw 中：`cmd.SetPushConstants`、`BindVertexBuffer`、`BindIndexBuffer`、`DrawIndexed`
- [ ] 确保 Renderable 持有 Material 非占有指针

---

## Phase 3：Deferred 管线（2 周）

### 3.1 Shadow Pass

- [ ] 声明 ShadowMap 纹理：`DeclareTexture("ShadowMap", {2048, 2048, Format::D32})`
- [ ] 实现 Shadow Pass：`AddPass("ShadowPass", setup, execute)`
- [ ] Setup 中 `b.WriteDepth(shadowMap)`
- [ ] Execute 中遍历 `GetDrawsForPass(PassFlags::ShadowCaster)` 绘制
- [ ] 实现 Shadow 相机矩阵（正交投影）传入 Pass
- [ ] Shadow Pass 无前置依赖，可最早执行

### 3.2 GBuffer Pass

- [ ] 声明 GBuffer 纹理：Albedo、Normal、Depth（RGBA8、RGBA16F、D24S8）
- [ ] 实现 GBuffer Pass：`AddPass("GBufferPass", setup, execute)`
- [ ] Setup 中 `WriteColor(0, gbufferAlbedo)`、`WriteColor(1, gbufferNormal)`、`WriteDepth(gbufferDepth)`
- [ ] Setup 中 `ReadTexture(shadowMap)` 声明依赖
- [ ] Execute 中遍历 `GetDrawsForPass(PassFlags::Opaque)` 绘制
- [ ] 依赖 Shadow Pass 完成

### 3.3 Lighting Pass

- [ ] 声明 Lighting 结果纹理：`DeclareTexture("Lighting", {1920, 1080, Format::RGBA16F})`
- [ ] 实现 Lighting Pass：`AddPass("LightingPass", setup, execute)`
- [ ] Setup 中 ReadTexture：gbufferAlbedo、gbufferNormal、gbufferDepth、shadowMap
- [ ] Setup 中 WriteColor(0, lightingResult)
- [ ] Execute 中全屏三角形 + PBR 光照计算（光照 UBO）
- [ ] 依赖 GBuffer Pass 完成

### 3.4 Post-Process Pass

- [ ] 声明 FinalColor 纹理：`DeclareTexture("FinalColor", {1920, 1080, Format::RGBA8})`
- [ ] 实现 PostProcess Pass：`AddPass("PostProcess", setup, execute)`
- [ ] Setup 中 ReadTexture(lightingResult)、WriteColor(0, finalColor)
- [ ] Execute 中实现 Bloom、Tone Mapping、FXAA
- [ ] 依赖 Lighting Pass 完成

### 3.5 OutputToSwapchain Pass

- [ ] 实现 `AddPass("OutputToSwapchain", setup, execute)`
- [ ] Setup 中 `ReadTexture(finalColor)`、`WriteSwapchain()`
- [ ] Execute 中 Blit/Copy finalColor → GetBackBuffer()
- [ ] 依赖 PostProcess Pass 完成

### 3.6 SetupRenderGraph 示例

- [ ] 实现完整的 SetupRenderGraph 函数（Deferred + Shadow 管线）
- [ ] 验证 Pass 依赖 DAG：Shadow → GBuffer → Lighting → PostProcess → Output
- [ ] 与分辨率 SetResolution 配合，DeclareTexture 使用当前分辨率

### 3.7 Pass 依赖推导

- [ ] ReadTexture(A) 且 WriteTexture(A) 的 Pass 之间：写者先于读者
- [ ] 同一纹理多写者：需显式顺序或合并 Pass
- [ ] 验证 topologicalOrder_ 正确反映依赖关系

---

## Phase 4：并行与优化（1–2 周）

### 4.1 Pass DAG 拓扑序分组

- [ ] 实现 `GetTopologicalGroups()` 按依赖分组
- [ ] 同组内 Pass 无依赖，可并行录制
- [ ] 组间按拓扑序串行执行
- [ ] 示例：Shadow 先执行；GBuffer 依赖 Shadow；Lighting 依赖 GBuffer 等

### 4.2 多线程命令录制

- [ ] 实现 `RenderGraph::RecordPasses` 中调用 `scheduler_->Submit` 并行录制
- [ ] 每线程独立 CommandList：`device->BeginCommandList(threadIndex)`
- [ ] 无依赖 Pass 同组内并行：`futures.push_back(scheduler_->Submit([&, i](){ ... }))`
- [ ] 收集所有 CommandList 后统一 Submit
- [ ] 集成 RenderTaskScheduler（executor 层）
- [ ] 构造函数 `RenderGraph(RenderTaskScheduler* scheduler)` 或 `SetScheduler(scheduler)`
- [ ] scheduler 为 nullptr 时退化为单线程录制

### 4.3 Vulkan 多线程录制约束

- [ ] 每 VkCommandBuffer 单线程录制
- [ ] 每线程使用独立 CommandPool
- [ ] RDI 的 `BeginCommandList(threadIndex)` 支持多线程
- [ ] 与 device_abstraction_layer 的 CommandPool 管理对齐

### 4.4 GPU Instancing 支持

- [ ] 实现批量渲染相同 Mesh 的多个实例
- [ ] 创建 Instance Buffer 存储 per-instance 数据（modelMatrix、color 等）
- [ ] 实现 `BindVertexBuffer(1, instanceBuffer)` 实例属性
- [ ] 实现 `DrawIndexed(indexCount, instanceCount)` 实例绘制
- [ ] 与 Material 的 AcquireInstanceDescriptorSet 配合或替代方案（大实例数时用 Storage Buffer）

### 4.5 帧流水线同步完善

- [ ] 每帧对应 Fence，Wait 上一帧 Fence 后再 Acquire
- [ ] Acquire 返回的 semaphore 用于 Submit 时 wait
- [ ] Submit 时 signal renderCompleteSemaphore，供 Present 使用（若需要）
- [ ] RDI 的 Submit 接口支持 `(cmdLists, waitSemaphores, signalSemaphores, fence)`

---

## Phase 5：高级特性（1–2 周）

### 5.1 Shader Manager

- [ ] 实现 `ShaderManager` 类
- [ ] 实现 `LoadShader(const std::string& path, ShaderStage stage, IRenderDevice* device)` 返回 ShaderHandle
- [ ] 实现 `GetShader(const std::string& name)` 查找
- [ ] 实现 `ReloadShader(const std::string& path)` 热重载
- [ ] 实现 `shaders_` 缓存（`std::unordered_map<std::string, ShaderHandle>`）
- [ ] Render Graph 或 Material 通过 ShaderManager 加载着色器

### 5.2 着色器热重载集成

- [ ] 集成文件监控（inotify/kqueue/FileSystemWatcher）或轮询
- [ ] 着色器文件变更时调用 ReloadShader
- [ ] 重新创建受影响的 Pipeline
- [ ] 与 resource_management_layer 的热重载对齐

### 5.3 多相机/多视口

- [ ] 支持 `Execute(IRenderDevice* device, RenderTarget* target)` 或类似重载
- [ ] 方案 A：每相机独立 RenderGraph 实例，Execute 时指定 RenderTarget
- [ ] 方案 B：单 RenderGraph 支持 Execute(cameraIndex, target)
- [ ] 方案 C：应用层对每相机分别 ClearSubmitted、SubmitRenderable、Execute 到不同 target
- [ ] CullScene 多相机返回 `std::vector<std::vector<SceneNode*>>`
- [ ] 应用层分别 SubmitRenderable 到各自的 Pass 链或 RenderTarget

### 5.4 Transparent Pass

- [ ] 实现 Transparent Pass：`AddPass("TransparentPass", setup, execute)`
- [ ] Setup 中依赖 Lighting 结果，WriteColor 到混合目标
- [ ] Execute 中遍历 `GetDrawsForPass(PassFlags::Transparent)`
- [ ] 实现透明物体排序（按深度或距离相机远近）
- [ ] 透明 Pass 插在 Lighting 与 PostProcess 之间，或 PostProcess 前
- [ ] Blend 状态：Alpha 混合

### 5.5 多光源 Shadow Pass

- [ ] 支持多光源时多个 Shadow Pass（如 Directional + Point Lights）
- [ ] 同组内多个 Shadow Pass 可并行录制
- [ ] 每个 Shadow Pass 写入独立 ShadowMap 或 Cascade

---

## 关键接口与流程

### 应用层 OnRender 流程

- [ ] 应用层 OnRender 中：`CullScene(camera)` → `rg->ClearSubmitted()` → 遍历 visibleNodes → `SubmitRenderable(r, worldMatrix, passFlags)` → `Execute(renderDevice)`
- [ ] 与 SceneManager 的 CullScene 输出对齐
- [ ] Present 由 Run 在 Execute 返回后调用，不在 Execute 内部

### Execute 完整流程

- [ ] `WaitForFence(frameFence)` → `ResetFence(frameFence)` → `AcquireNextImage()` → `BuildFrameDrawList()` → `RecordPasses()` → `Submit(cmdLists, waitSem, signalSem, fence)` → `ReleaseFrameResources()` → `currentFrameIndex_` 轮转

### RenderEngine::Run 主循环

- [ ] 主循环中：`app->OnRender()` 内部调用 `rg->Execute()`，之后 `renderDevice_->Present()`
- [ ] 与 Run 主循环集成

---

## 与上层集成

### RenderEngine 初始化

- [ ] `renderGraph_ = std::make_unique<RenderGraph>(scheduler_.get())` 或 `SetScheduler`
- [ ] `renderGraph_->SetResolution(config.width, config.height)`
- [ ] `SetupRenderGraph(*renderGraph_)` 应用层或引擎初始化时调用
- [ ] `renderGraph_->Compile(renderDevice_.get())` 初始化完成时调用
- [ ] 分辨率变化时重新 Compile

### 依赖关系

| 上层模块 | 依赖渲染管线层 |
|----------|----------------|
| Application | RenderGraph (SubmitRenderable, Execute) |
| Scene Manager | CullScene 输出供 SubmitRenderable |

| 渲染管线层依赖 | 模块 |
|----------------|------|
| IRenderDevice | 设备抽象层 |
| RenderTaskScheduler | 执行器层 |
| Material, Texture, Shader | 资源管理层 |
| Renderable | 场景管理层 |

---

## 错误处理与生命周期

### Compile 失败

- [ ] Compile 在分辨率变化或首次 Setup 时调用
- [ ] 失败时返回 false 或抛出，GetLastError 获取原因
- [ ] 资源分配失败时保证已分配资源可释放

### 实例级 DescriptorSet 回收

- [ ] 每帧 Execute 结束时调用 ReleaseFrameResources
- [ ] 遍历本帧 SubmittedDraws 中的 Material，调用 ReleaseAllInstanceDescriptorSets
- [ ] 池化复用，避免每帧大量分配

### 帧流水线同步

- [ ] 每帧对应 Fence，Wait 上一帧 Fence 后再 Acquire
- [ ] Acquire 返回的 semaphore 用于 Submit 时 wait
- [ ] Submit 时 signal renderCompleteSemaphore，供 Present 使用（若 Present 需要）

### Renderable 与 Material 生命周期

- [ ] Renderable 由应用层或工厂创建，持有 Material 非占有指针
- [ ] Material 由 ResourceManager 加载或应用层创建
- [ ] Mesh/Material 由资源层加载，Renderable 持有非占有指针

---

## 附录：资源句柄与 Pass 依赖

### RGResourceHandle 与 RDI 映射

- [ ] RenderGraph 内部使用逻辑句柄（字符串名或 ID）
- [ ] Compile 时解析依赖，分配实际 GPU 资源并建立 RGResourceHandle → RDI Handle 映射
- [ ] 与 RDI 的 TextureHandle/BufferHandle 区分

### 技术栈

- **glm**：数学库（mat4, vec3）
- **executor**：并行录制调度，见 executor_layer_todolist
- **RDI**：设备抽象层，见 device_abstraction_layer_todolist

---

## 参考文档

- [rendering_pipeline_layer_design.md](../design/rendering_pipeline_layer_design.md) - 渲染管线层完整设计
- [rendering_engine_design.md](../design/rendering_engine_design.md) - 渲染引擎架构
- [rendering_engine_code_examples.md](../design/rendering_engine_code_examples.md) - 代码示例
- [executor_layer_design.md](../design/executor_layer_design.md) - 并行录制协作
- [device_abstraction_layer_design.md](../design/device_abstraction_layer_design.md) - RDI 接口
- [scene_management_layer_design.md](../design/scene_management_layer_design.md) - CullScene 与 SubmitRenderable
- [resource_management_layer_design.md](../design/resource_management_layer_design.md) - Material、Shader、Texture 加载
