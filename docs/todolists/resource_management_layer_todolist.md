# 资源管理层 (Resource Management Layer) 实现任务清单

> 基于 [resource_management_layer_design.md](../design/resource_management_layer_design.md)、[rendering_engine_design.md](../design/rendering_engine_design.md)、[rendering_engine_code_examples.md](../design/rendering_engine_code_examples.md) 设计文档构造。

## 设计目标

- **非阻塞主循环**：异步加载通过 executor 在后台执行，LoadAsync 返回 Future，不阻塞主线程
- **占位符策略**：资源未就绪时使用占位符或跳过绘制，并触发 LoadAsync（若尚未触发）
- **统一缓存**：所有资源类型通过 ResourceCache 统一管理，支持路径去重、引用计数
- **Staging 复用**：Staging Buffer 池化，避免每帧大量分配；Upload Queue 与 RDI 配合完成 CPU→GPU 传输
- **热重载**：开发期支持文件变化侦测与资源热重载

---

## Phase 1：基础框架（1–2 周）

### 1.1 资源句柄类型

- [x] 实现 `ResourceHandle<T>` 模板（id, IsValid, operator==, operator!=）
- [x] 实现 `ResourceHandleAny` 类型擦除句柄（id + type_index）
- [x] 实现 `ToAny(ResourceHandle<T>)` 转换为 ResourceHandleAny
- [x] 定义 Mesh、Texture、Material 等资源句柄类型别名

### 1.2 ResourceCache 基础实现

- [x] 实现 `CacheEntry` 结构（resource, path, refCount, isReady, typeId）
- [x] 实现 `Register<T>(path, resource, ready)` 登记资源
- [x] 实现 `RegisterPlaceholder<T>(path)` 预注册占位条目
- [x] 实现 `Get<T>(handle)` 获取资源
- [x] 实现 `IsReady<T>(handle)` 检查就绪状态
- [x] 实现 `SetResource(handle, resource)` 和 `SetReady(handle)`
- [x] 实现 `FindByPath(path, typeId)` 路径查找
- [x] 实现 `AddRef` / `Release` 引用计数
- [x] 线程安全：entries_、pathToId_ 的 mutex 保护

### 1.3 IResourceLoader 接口

- [x] 实现 `IResourceLoader` 纯虚基类
- [x] 实现 `Supports(path)` 判断是否支持该路径
- [x] 实现 `Load(path, ResourceLoadContext& ctx)` 同步加载
- [x] 实现 `GetResourceType()` 返回 type_index
- [x] 定义 `ResourceLoadContext` 结构（device, stagingMgr, resourceManager）

### 1.4 ResourceManager 接口与 Loader 注册

- [x] 实现 `ResourceManager` 构造函数（scheduler, device, stagingMgr）
- [x] 实现 `RegisterLoader(std::unique_ptr<IResourceLoader> loader)`
- [x] 实现 `FindLoader(path, typeId)` 查找 Loader
- [x] 实现 `SetAssetPath(path)` 设置资源根路径
- [x] 实现 `AddPathAlias(alias, path)` 路径别名
- [x] 实现 `ResolvePath(path)` 解析完整路径

### 1.5 同步 Load 实现

- [x] 实现 `Load<T>(path)` 模板方法
- [x] 检查缓存：若已存在则 AddRef 并返回
- [x] 查找 Loader 并调用 Load
- [x] 加载成功后 Register 入 Cache
- [x] 加载失败时 `GetLastError()` 返回原因
- [x] 失败不缓存，避免重复加载失败路径

### 1.6 简单 TextureLoader

- [x] 实现 `TextureLoader : public IResourceLoader`
- [x] 支持 .png、.jpg（使用 stb_image）
- [x] 实现 `LoadSTB(path)` 加载未压缩纹理
- [x] 创建 RDI Texture，直接传入数据（暂不通过 Staging）
- [x] 返回 `std::unique_ptr<Texture>` 或等价的 std::any

### 1.7 简单 ModelLoader

- [x] 实现 `ModelLoader : public IResourceLoader`
- [x] 支持 .gltf（使用 tinygltf）
- [x] 实现 `LoadGLTF(path)` 解析顶点/索引
- [x] 创建 vertexBuffer、indexBuffer 通过 RDI
- [x] 生成 Mesh 结构（vertexBuffer, indexBuffer, indexCount, vertexCount, bounds, subMeshes）
- [x] 可选：支持 .obj（使用 assimp 或简易解析）

### 1.8 Mesh 与 Texture 数据结构

- [x] 定义 `Mesh` 结构（vertexBuffer, indexBuffer, indexCount, vertexCount, topology, bounds, subMeshes）
- [x] 定义 `SubMesh` 结构（indexOffset, indexCount, materialIndex）
- [x] 定义 `Texture` 结构（handle, width, height, format, mipLevels）
- [x] 定义 `BoundingBox` 结构

---

## Phase 2：异步加载（1–2 周）

### 2.1 LoadAsync 与 executor 集成

- [x] 实现 `LoadAsync<T>(path)` 模板方法
- [x] 检查缓存：若已存在则 `MakeReadyFuture(handle)` 返回
- [x] 预注册占位条目：`RegisterPlaceholder<T>(path)`
- [x] 提交异步任务：`scheduler_->Submit([...]() -> ResourceHandle<T>)`
- [x] 任务内：FindLoader → Load → SetResource → SetReady
- [x] 返回 `Future<ResourceHandle<T>>`

### 2.2 Future 返回与回调

- [x] 确保 Future 与 executor 的 submit 兼容
- [x] 实现 `ProcessLoadedCallbacks()`（可选，供主循环调用）
- [x] 加载失败时 Future 传递错误（异常或错误状态）

### 2.3 占位符系统

- [x] 实现 `CreatePlaceholders()` 创建占位符资源
- [x] 实现 `GetPlaceholderMesh()` 返回占位符 Mesh
- [x] 实现 `GetPlaceholderTexture()` 返回占位符 Texture
- [x] 实现 `GetPlaceholderMaterial()` 返回占位符 Material
- [x] 占位符：简单几何体（如三角形/立方体）、默认纹理（1x1 纯色）、默认材质

### 2.4 Draw 时资源检查与触发加载

- [x] 实现 `IsReady<T>(handle)` 供上层检查
- [x] 实现 `Get<T>(handle)` 未就绪返回 nullptr
- [x] 设计文档示例：StaticMesh::Draw 中若 mesh/material 未就绪则用占位符并触发 LoadAsync
- [x] 避免重复触发：LoadAsync 前检查是否已在加载中（占位条目存在即视为已触发）

---

## Phase 3：Staging 与上传（1–2 周）

### 3.1 StagingMemoryManager 实现

- [x] 实现 `StagingMemoryManager(IRenderDevice* device)` 构造函数
- [x] 定义 `StagingAllocation` 结构（buffer, mappedPtr, size, offset）
- [x] 实现 `Allocate(size)` 从池分配
- [x] 实现 `Free(alloc)` 回收到池
- [x] 实现 Staging Buffer 池初始化（默认 64MB poolSize_）

### 3.2 Staging Buffer 池化

- [x] 维护 `stagingPool_` 预分配 Buffer 列表
- [x] 实现线性分配或块分配策略
- [x] 分配不足时扩展池或等待 GPU 完成回收
- [x] 与 Fence 关联：GPU 完成时 Free 回池

### 3.3 Upload Queue 与 Copy 命令

- [x] 实现 `SubmitUpload(cmd, src, dstTexture, mipLevel)` Buffer→Texture
- [x] 实现 `SubmitUpload(cmd, src, dstBuffer, dstOffset)` Buffer→Buffer
- [x] 收集 `pendingUploads_` 待执行上传
- [x] 实现 `FlushUploads(device)` 在 Execute 前提交 Copy 命令

### 3.4 TextureLoader 集成 Staging 上传

- [x] TextureLoader 使用 StagingMemoryManager::Allocate 获取暂存缓冲
- [x] 将解码后的像素数据写入 mappedPtr
- [x] 通过 SubmitUpload 提交 Copy 到 GPU Texture
- [x] 等待 GPU 完成后 Free StagingAllocation
- [x] 支持 Mipmap 链上传（逐级 Copy）

### 3.5 ModelLoader 集成 Staging 上传

- [x] ModelLoader 顶点/索引数据通过 Staging 上传
- [x] Allocate 获取 StagingAllocation
- [x] SubmitUpload 到 vertexBuffer、indexBuffer
- [x] 上传完成后 Free

---

## Phase 4：完整 Loader（2 周）

### 4.1 完整 ModelLoader

- [x] 支持 glTF 材质引用
- [x] 解析 glTF 中的 material 索引，关联 Material 路径
- [x] 支持 SubMesh 与材质映射
- [x] 可选：支持 .obj、.fbx（通过 assimp）
- [x] 支持 LOD（若有多个 mesh）

### 4.2 MaterialLoader

- [x] 实现 `MaterialLoader : public IResourceLoader`
- [x] 实现 `LoadJSON(path)` 解析 JSON 材质定义
- [x] 格式：`{ "albedo": "textures/brick.png", "metallic": 0.2, ... }`
- [x] 依赖加载：解析 textures 数组，调用 `ctx.resourceManager->Load<Texture>(texPath)`
- [x] 创建 Material 并设置纹理句柄与参数
- [x] 返回 `std::unique_ptr<Material>`

### 4.3 ShaderCompiler

- [x] 实现 `ShaderCompiler` 类（非 IResourceLoader，独立接口）
- [x] 实现 `Compile(path, stage, device)` 加载并编译
- [x] 实现 `LoadSPIRV(path)` 加载 .spv 文件
- [x] 实现 `CompileGLSLToSPIRV(path, stage)` 使用 glslang/shaderc
- [x] 支持 .vert、.frag、.comp、.spv
- [x] 实现 `Recompile(path, stage, device)` 热重载时重新编译

### 4.4 压缩纹理支持

- [x] TextureLoader 支持 .ktx（KTX1 内联解析，glInternalFormat→RDI Format）
- [ ] TextureLoader 支持 .dds（BC、ASTC、ETC2 等）
- [x] 实现 `LoadKTX(path)` 加载 KTX 格式
- [ ] 实现 `LoadDDS(path)` 加载 DDS 格式（可选）
- [x] 根据格式选择 RDI CreateTexture 的 Format

### 4.5 资源依赖解析

- [x] MaterialLoader 内部解析 JSON 时引用 Texture 路径
- [x] 同步加载依赖：`ctx.resourceManager->Load<Texture>(texPath)`
- [x] 或加入依赖队列，异步加载后再设置
- [x] 处理循环依赖（材质 A 依赖材质 B 依赖纹理 C）

---

## Phase 5：热重载与优化（1 周）

### 5.1 文件变化侦测

- [x] 实现 `EnableHotReload(bool enable)` 开关
- [x] 实现 `ProcessHotReload()` 每帧调用
- [x] 检查已加载资源的文件时间戳
- [x] 使用 inotify/watchdog 或轮询 GetFileModificationTime
- [x] 记录 path → lastModified 映射

### 5.2 热重载流程

- [x] 检测到文件变化时重新 Load
- [x] 替换 Cache 中的资源（SetResource）
- [x] 若正在使用则等待下一帧或同步
- [x] 材质/着色器热重载：替换后重新创建 Pipeline
- [x] ShaderCompiler::Recompile 集成

### 5.3 引用计数与延迟释放

- [x] Register 时 refCount=1
- [x] AddRef 增；Release 减
- [x] refCount=0 时加入待释放队列
- [x] 下一帧统一 Destroy（避免渲染中使用时被释放）
- [x] 实现 `Unload(ResourceHandleAny handle)` 释放资源

### 5.4 预加载与批量加载

- [x] 实现 `Preload(paths)` 预加载一批资源
- [x] 实现 `LoadAsyncBatch<T>(paths)` 返回 `std::vector<Future<ResourceHandle<T>>>`
- [x] 场景切换前调用 Preload 预加载新场景资源

### 5.5 性能测试与调优

- [x] 验证 LoadAsync 不阻塞主线程
- [x] 验证 Staging Buffer 池化无频繁分配
- [x] 验证引用计数正确释放
- [x] 热重载性能：文件变化到资源更新延迟

---

## 与上层集成

### RenderEngine 初始化

- [x] `StagingMemoryManager` 在 ResourceManager 之前创建
- [x] `ResourceManager(scheduler, device, stagingMgr)` 构造
- [x] `resourceManager_->SetAssetPath(config.assetPath)`
- [x] 注册 Loader：ModelLoader、TextureLoader、MaterialLoader
- [x] `CreatePlaceholders()` 创建占位符

### 主循环中的资源处理

- [x] `Run()` 中每帧调用 `resourceManager_->ProcessHotReload()`
- [x] 可选：`ProcessLoadedCallbacks()` 处理加载完成回调
- [x] 与 RenderEngine::Run 流程对齐

### 依赖关系

| 上层模块 | 依赖资源管理层 |
|----------|----------------|
| Render Graph | Material, Texture, Shader |
| Scene Manager | Mesh |
| Application | 所有资源类型 |
| Material System | Texture, Shader |

### 下层依赖

| 资源管理层组件 | 依赖下层 |
|---------------|----------|
| ResourceManager | RenderTaskScheduler, IRenderDevice |
| StagingMemoryManager | IRenderDevice |
| Loaders | IRenderDevice, StagingMemoryManager |
| ModelLoader / TextureLoader | StagingMemoryManager（上传） |
| MaterialLoader | ResourceManager（依赖 Texture） |
| ShaderCompiler | IRenderDevice |

---

## 错误处理与生命周期

### 加载失败

- [ ] `Load()` 返回空句柄，`GetLastError()` 返回原因
- [ ] `LoadAsync()` 的 Future 通过异常或错误状态传递失败
- [ ] 失败时不缓存，避免重复加载失败路径

### Staging Buffer 生命周期

- [ ] Allocate 从池中取或扩展
- [ ] Upload 提交后需等待 GPU 完成再回收
- [ ] 与 Fence 关联，GPU 完成时 Free 回池

### 资源释放时机

- [ ] 释放仅在帧边界或显式 Unload 时
- [ ] 避免渲染中使用时被释放：refCount=0 延迟到下一帧

---

## 技术栈

### 推荐第三方库

- [ ] **tinygltf**：glTF 解析
- [ ] **stb_image**：PNG/JPG 加载
- [ ] **basis_universal**：KTX 压缩纹理
- [ ] **assimp**：多格式模型（可选）
- [ ] **glslang** / **shaderc**：GLSL→SPIR-V 编译

---

## 参考文档

- [resource_management_layer_design.md](../design/resource_management_layer_design.md) - 资源管理层完整设计
- [rendering_engine_design.md](../design/rendering_engine_design.md) - 渲染引擎架构
- [rendering_engine_code_examples.md](../design/rendering_engine_code_examples.md) - 代码示例
- [executor_layer_design.md](../design/executor_layer_design.md) - 异步加载协作
- [device_abstraction_layer_design.md](../design/device_abstraction_layer_design.md) - RDI 与 Staging 协作
