# 场景管理层 (Scene Management Layer) 实现任务清单

> 基于 [scene_management_layer_design.md](../design/scene_management_layer_design.md)、[rendering_engine_design.md](../design/rendering_engine_design.md)、[rendering_engine_code_examples.md](../design/rendering_engine_code_examples.md) 设计文档构造。

## 设计目标

- **职责清晰**：Scene Graph 管 Transform 与剔除，ECS 管逻辑，二者通过 SceneNodeRef 桥接
- **更新顺序固定**：ECS::Update → 逻辑系统写回 Scene Graph → SceneManager::Update → OnUpdate → OnRender
- **生命周期安全**：SceneNodeHandle 句柄化，场景切换时避免悬空引用
- **剔除一体化**：视锥剔除、遮挡剔除、LOD 选择均内含于 SceneManager::CullScene
- **多相机支持**：CullScene 可接受多相机，返回按相机分组的可见列表

---

## Phase 1：Scene Graph 基础（1–2 周）

### 1.1 句柄与类型定义

- [x] 定义 `SceneNodeHandle = uint64_t` 类型
- [x] 定义 `kInvalidSceneNodeHandle = 0` 常量
- [x] 实现 `SceneManager::GetHandle(SceneNode* node)` 句柄查找
- [x] 实现 `SceneManager::GetNode(SceneNodeHandle handle)` 句柄解析（已销毁返回 `nullptr`）
- [x] 实现 `handleRegistry_`（`std::unordered_map<SceneNodeHandle, SceneNode*>`）
- [x] 节点创建时分配 handle 并注册，销毁时从注册表移除

### 1.2 SceneNode 核心

- [x] 实现 `SceneNode` 类
- [x] 实现 `localTransform_`、`worldMatrix_`（glm::mat4）
- [x] 实现 `SetLocalTransform(const glm::mat4& t)` / `GetLocalTransform()`
- [x] 实现 `GetWorldMatrix()`（由 SceneManager::Update 计算后只读）
- [x] 实现 `SetWorldMatrix(const glm::mat4& m)`（friend 关系，仅供 SceneManager 调用）
- [x] 实现 `AddChild(std::unique_ptr<SceneNode> child)` 返回 `SceneNode*`
- [x] 实现 `GetParent()` / `GetChildren()` 父子层级访问
- [x] 实现 `GetHandle()` 返回 SceneNodeHandle
- [x] 实现 `handle_` 成员，由 SceneManager 在 CreateScene/AddChild 时设置

### 1.3 SceneManager 生命周期

- [x] 实现 `SceneManager::CreateScene()` 创建根节点、分配 handle、注册
- [x] 实现 `SetActiveScene(SceneNode* root)` 销毁旧场景、激活新场景
- [x] 实现 `GetActiveRoot()` 返回当前活动根
- [x] 销毁旧场景时递归销毁节点并从 handleRegistry 移除
- [x] 实现 `nextHandle_` 分配递增 handle

### 1.4 世界矩阵计算

- [x] 实现 `SceneManager::Update(float deltaTime)`
- [x] 实现 `UpdateRecursive(SceneNode* node, const glm::mat4& parentWorld)`
- [x] 世界矩阵计算：`world = parentWorld * node->GetLocalTransform()`
- [x] 递归更新所有子节点

### 1.5 Pass 标志

- [x] 定义 `PassFlags` 枚举（ShadowCaster、Opaque、Transparent、All）
- [x] SceneNode 实现 `SetPassFlags(PassFlags f)` / `GetPassFlags()`
- [x] 默认 `PassFlags::All`

### 1.6 Renderable 挂载

- [x] SceneNode 实现 `SetRenderable(Renderable* r)` / `GetRenderable()`
- [x] SceneNode 持有 Renderable 非占有指针（`Renderable* renderable`）

---

## Phase 2：剔除系统（1 周）

### 2.1 BoundingBox 与数学

- [x] 实现 `BoundingBox` 结构（min, max，glm::vec3）
- [x] 实现 `BoundingBox::Transform(const glm::mat4& m)` 变换包围体
- [x] 实现 `TransformBounds(const BoundingBox& box, const glm::mat4& m)` 自由函数

### 2.2 视锥剔除

- [x] 实现 `FrustumPlanes` 结构（planes[6]，left/right/bottom/top/near/far）
- [x] 实现 `ExtractFrustumPlanes(const glm::mat4& viewProj)` 从 VP 矩阵提取
- [x] 实现 `IsBoundsInFrustum(const BoundingBox& bounds, const FrustumPlanes& frustum)`

### 2.3 CullScene 单相机

- [x] 实现 `SceneManager::CullScene(CameraNode* camera)` 返回 `std::vector<SceneNode*>`
- [x] 递归遍历场景图
- [x] 无 Renderable 的节点：只遍历子节点，不加入可见列表
- [x] 有 Renderable 的节点：用 `TransformBounds` 计算世界包围体，视锥测试
- [x] 通过视锥测试的节点加入 `visibleNodes`
- [x] 需访问 node->children：SceneManager 为 SceneNode 的 friend 或提供访问接口

### 2.4 Renderable 抽象

- [x] 实现 `Renderable` 基类
- [x] 实现 `GetBounds()` 返回 BoundingBox
- [x] 实现 `GetMesh()` / `GetMaterial()`（或 mesh、material 成员）
- [x] 实现 `Draw(CommandList& cmd, const glm::mat4& worldTransform)` 虚函数
- [x] 定义 `bounds` 成员供 CullScene 使用

---

## Phase 3：ECS 与桥接（1–2 周）

### 3.1 SceneNodeRef 桥接

- [x] 实现 `SceneNodeRef` 结构
- [x] 实现 `handle` 成员（SceneNodeHandle）
- [x] 实现 `IsValid()` 检查 handle != kInvalidSceneNodeHandle
- [x] 实现 `GetNode(SceneManager* sceneMgr)` 解析为 SceneNode*
- [x] 实现 `SceneNodeRef::FromNode(SceneNode* node)` 工厂
- [x] System 中调用 `GetNode` 后必须校验 `if (!node) continue;`

### 3.2 Entity 与 ComponentStorage

- [x] 实现 `Entity` 结构（id, generation，IsValid）
- [x] 定义 `Entity::Null` 常量
- [x] 实现 `ComponentStorage<T>` 模板
- [x] 实现 `Add(Entity, T)` / `Remove(Entity)` / `Get(Entity)` / `Has(Entity)`
- [x] 实现 `entityToIndex_` 映射

### 3.3 EntityManager

- [x] 实现 `EntityManager` 类
- [x] 构造函数 `EntityManager(RenderTaskScheduler* scheduler, SceneManager* sceneMgr = nullptr)`
- [x] 实现 `SetSceneManager(SceneManager*)` / `GetSceneManager()`
- [x] 实现 `CreateEntity()` / `DestroyEntity(Entity)` / `IsAlive(Entity)`
- [x] 实现 `Update(float deltaTime)` 根据 GetDependencies 构建 DAG，提交 executor
- [x] 实现 `AddComponent<T>` / `GetComponent<T>` / `HasComponent<T>` / `RemoveComponent`
- [x] 实现 `EntitiesWith<Components...>()` 查询
- [x] 实现 `RegisterSystem(std::unique_ptr<System>)`

### 3.4 System 基类

- [ ] 实现 `System` 基类
- [ ] 纯虚 `Update(float deltaTime, EntityManager& em)`
- [ ] 可选 `OnEntityCreated(Entity)` / `OnEntityDestroyed(Entity)`
- [ ] 实现 `GetDependencies()` 返回 `std::vector<std::type_index>`（默认空）

### 3.5 与 executor 集成

- [ ] 根据 `System::GetDependencies()` 构建 System 依赖 DAG
- [ ] EntityManager::Update 通过 RenderTaskScheduler 提交并行 System 任务
- [ ] 保证依赖的 System 先执行（拓扑序）
- [ ] 与 executor_layer_todolist 中 RenderTaskScheduler 的 SubmitSystemUpdate 对齐

### 3.6 写回流程示例

- [ ] 实现 `PhysicsSystem` 示例：读取 PhysicsComponent，通过 SceneNodeRef 写回 `SetLocalTransform`
- [ ] 实现 `AnimationSystem` 示例：声明依赖 PhysicsSystem
- [ ] 验证 GetDependencies 建立的 DAG 正确性

### 3.7 写回冲突检测（可选）

- [ ] `#ifdef ENABLE_SCENE_WRITE_VALIDATION` 时启用
- [ ] 实现 `NotifySceneNodeWritten(handle, systemTypeId)` 登记
- [ ] 帧末检查：同一节点被多个无依赖关系的 System 写入时断言或日志

---

## Phase 4：Camera 与多相机（1 周）

### 4.1 CameraNode

- [ ] 实现 `CameraNode : public SceneNode`
- [ ] 实现 `fov`、`nearPlane`、`farPlane` 成员
- [ ] 实现 `viewMatrix`、`projectionMatrix` 成员
- [ ] 实现 `UpdateViewProjection()` 由应用层或系统在需要时调用

### 4.2 CullScene 多相机

- [ ] 实现 `CullScene(const std::vector<CameraNode*>& cameras)` 返回 `std::vector<std::vector<SceneNode*>>`
- [ ] 每个相机对应一组可见节点
- [ ] `visibleByCamera[0]` 对应 `cameras[0]`，依此类推

### 4.3 多视口支持

- [ ] 应用层可分别 SubmitRenderable 到各自 RenderTarget 或 Pass 链
- [ ] 与 Render Graph 多套 Pass 链输出到不同 RenderTarget 对齐（小地图、分屏等）

### 4.4 工厂函数

- [ ] 实现 `CreateStaticMeshNode(Mesh* mesh, Material* material)` 返回 `unique_ptr<SceneNode>`
- [ ] 实现 `CreateCameraNode()` 返回 `unique_ptr<CameraNode>`

---

## Phase 5：LOD 与高级剔除（1–2 周）

### 5.1 LOD Manager 集成

- [ ] 实现或集成 `LODManager`
- [ ] CullScene 内调用 `lodManager_->SelectLOD(node, camera)`
- [ ] LOD 选择后写入 Renderable 的 currentLOD 或 SceneNode 的 LOD 索引
- [ ] 支持 LOD 的 Renderable（如 StaticMesh）实现 `GetMesh()` 重写以返回选定 LOD 的 mesh
- [ ] StaticMesh 持有 meshLODs 和 currentLOD

### 5.2 遮挡剔除（可选，Phase 6）

- [ ] 实现 `enableOcclusionCulling_` 开关
- [ ] 实现 `OcclusionCull(visibleNodes, hiZBuffer_)` 依赖 Hi-Z Buffer
- [ ] CullScene 中视锥剔除后可选调用遮挡剔除

### 5.3 场景切换与悬空引用

- [ ] 实现 `SwitchToNewLevel` 流程：先解绑旧场景 SceneNodeRef → SetActiveScene → 重新绑定
- [ ] 实现 `IsDescendantOf(parent, node)` 判断节点是否属于某子树
- [ ] Debug 模式下 SetActiveScene 可遍历 Entity 的 SceneNodeRef，检查是否有 handle 指向即将销毁的节点
- [ ] 若有则断言或日志，强制调用方先解绑

### 5.4 UpdateBounds（可选）

- [ ] 世界矩阵计算后可调用 UpdateBounds 更新 Renderable 的世界包围体缓存
- [ ] 设计文档中 UpdateBounds 由 SceneManager::Update 内部完成

---

## 关键接口与流程

### 应用层 OnRender 流程

- [ ] 应用层 OnRender 中：`CullScene(camera)` → `rg->ClearSubmitted()` → 遍历 visibleNodes → `SubmitRenderable(r, worldMatrix, passFlags)` → `Execute(renderDevice)`
- [ ] 与 Render Graph 的 SubmitRenderable 接口对齐

### 场景切换流程

- [ ] 遍历 `EntitiesWith<SceneNodeRef>`，若 handle 指向旧场景子树则 `RemoveComponent<SceneNodeRef>`
- [ ] 调用 `SetActiveScene(newSceneRoot)`
- [ ] 为新场景中需要逻辑控制的节点重新绑定 SceneNodeRef

### 统一更新顺序

- [ ] 主循环 Run() 中顺序：`inputManager->Update()` → `entityManager->Update(deltaTime)` → `sceneManager->Update(deltaTime)` → `app->OnUpdate(deltaTime)` → `app->OnRender()` → `renderDevice->Present()`
- [ ] 与 RenderEngine 主循环集成

---

## 与上层集成

### RenderEngine 初始化

- [ ] `sceneManager_ = std::make_unique<SceneManager>()`
- [ ] `entityManager_ = std::make_unique<EntityManager>(scheduler_.get(), sceneManager_.get())`
- [ ] 注册系统：`RegisterSystem(PhysicsSystem)`、`RegisterSystem(AnimationSystem)` 等

### 依赖关系

| 上层模块 | 依赖场景管理层 |
|----------|----------------|
| Application | SceneManager, EntityManager |
| Render Graph | 接收 CullScene 输出的可见节点，SubmitRenderable |
| 资源管理层 | Renderable 引用 Mesh/Material |
| Executor Layer | EntityManager 通过 RenderTaskScheduler 并行执行 System |

---

## 错误处理与生命周期

### SceneNodeHandle 失效

- [ ] 节点销毁时 handle 从注册表移除，`GetNode(handle)` 返回 nullptr
- [ ] System 必须校验 `if (!node) continue;`
- [ ] 场景切换前必须解绑 SceneNodeRef

### Renderable 所有权

- [ ] Renderable 由应用层或工厂创建
- [ ] SceneNode 持有 Renderable 非占有指针
- [ ] Mesh/Material 由 ResourceManager 加载并缓存，Renderable 持有非占有指针
- [ ] 多 SceneNode 可共享同一 Renderable（实例化）

### 写回竞争

- [ ] 同一 SceneNode 同一帧内仅应有一个 System 作为「主写者」
- [ ] 多 System 写同一节点时，必须通过 GetDependencies 建立 DAG 保证先后顺序
- [ ] 开发期可选冲突检测（ENABLE_SCENE_WRITE_VALIDATION）

---

## 附录：技术栈

- **glm**：数学库（mat4, vec3, vec4）
- **executor**：System 并行调度，见 executor_layer_todolist
- **Renderable**：与 resource_management_layer、rendering_pipeline_layer 协作

---

## 参考文档

- [scene_management_layer_design.md](../design/scene_management_layer_design.md) - 场景管理层完整设计
- [rendering_engine_design.md](../design/rendering_engine_design.md) - 渲染引擎架构
- [rendering_engine_code_examples.md](../design/rendering_engine_code_examples.md) - 代码示例
- [executor_layer_design.md](../design/executor_layer_design.md) - ECS System 并行执行协作
- [resource_management_layer_design.md](../design/resource_management_layer_design.md) - Renderable 引用 Mesh/Material
