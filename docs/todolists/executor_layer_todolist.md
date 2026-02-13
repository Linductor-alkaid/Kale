# 底层执行器层 (Executor Layer) 实现任务清单

> 基于 [executor_layer_design.md](../docs/design/executor_layer_design.md)、[rendering_engine_design.md](../docs/design/rendering_engine_design.md)、[rendering_engine_code_examples.md](../docs/design/rendering_engine_code_examples.md) 及 [executor/README.md](../docs/executor/README.md) 设计文档构造。

## 设计目标

- **无死锁**：避免因锁顺序、循环等待导致的死锁
- **无数据竞争**：保证对共享数据的访问符合内存模型，无未定义行为
- **基于 executor**：在 executor 库之上扩展，不修改其核心实现
- **高性能**：最小化同步开销，支持无锁/低锁路径

---

## Phase 1：基础通道（1–2 周）

### 1.1 TaskChannel 核心实现

- [x] 实现 `TaskChannel<T, Capacity>` SPSC 无锁队列
- [x] 实现 `try_send(T&& value)` 非阻塞发送
- [x] 实现 `try_recv(T& out)` 非阻塞接收
- [x] 实现 `send(T&& value, timeout)` 阻塞发送（带超时）
- [x] 实现 `recv(T& out, timeout)` 阻塞接收（带超时）
- [x] 实现 `size()` 和 `empty()` 查询接口
- [x] 基于 `std::atomic` 和 CAS 实现无锁环形缓冲区
- [x] 使用固定容量，避免动态分配

### 1.2 扩展与测试

- [x] 实现 MPSC（多生产者单消费者）变体（可选）
- [x] 单元测试：多线程 send/recv，无数据竞争
- [x] 单元测试：高并发压力测试
- [x] 保持与 executor 解耦，可独立使用

### 1.3 无锁实现要点（附录 A）

- [x] 使用 `std::atomic<size_t>` 表示 head/tail
- [x] `try_send`：tail 循环 CAS 推进，保证单生产者
- [x] `try_recv`：head 循环 CAS 推进，保证单消费者
- [x] 可选：`std::atomic_thread_fence` 保证可见性
- [x] 避免 ABA 问题（tag/generation 或 2 的幂容量）

---

## Phase 2：ExecutorFuture 增强（1 周）

### 2.1 ExecutorPromise / ExecutorFuture

- [x] 实现 `ExecutorPromise<T>` 模板类
- [x] 实现 `set_value(T value)` 和 `set_exception(std::exception_ptr e)`
- [x] 实现 `ExecutorFuture<T>::get_future()` 获取 future
- [x] 实现 `ExecutorFuture<T>::get()` 阻塞直到就绪
- [x] 实现 `ExecutorFuture<T>::valid()` 有效性检查

### 2.2 then 续接

- [x] 实现 `then(Executor& ex, F&& func)` 在 executor 中续接
- [x] 返回 `ExecutorFuture<std::invoke_result_t<F, T>>`
- [x] 避免阻塞当前线程，由 executor 调度续接任务

### 2.3 集成与 API

- [x] 实现 `async_load<T>(Executor& ex, std::function<T()> loader)` API
- [ ] 集成到 `LoadResourceAsync` 等上层 API
- [x] 确保 `promise::set_value` 仅调用一次，标准库保证线程安全

---

## Phase 3：TaskGraph 与 DataSlot（2–3 周）

### 3.1 DataSlot 与 TaskDataManager

- [x] 定义 `DataSlotHandle` 结构（id + generation）
- [x] 实现 `TaskDataManager::allocate_slot(size_t size_bytes)`
- [x] 实现 `TaskDataManager::get_slot(DataSlotHandle h)`
- [x] 实现 `TaskDataManager::release_slot(DataSlotHandle h)`
- [x] 实现 `TaskDataManager::bind_dependency(task_a, slot_a_out, task_b, slot_b_in)`
- [x] 输入槽在任务开始前填充，任务执行期间只读
- [x] 输出槽在任务执行期间独占写入，完成后生效

### 3.2 TaskGraph

- [x] 实现 `TaskGraph` 类
- [x] 定义 `TaskFunc = std::function<void(const TaskContext&)>`
- [x] 实现 `add_task(TaskFunc func, dependencies)` 添加任务节点
- [x] 实现 `add_task_with_data(TaskFunc func, deps, dependents)` 带数据依赖
- [x] 实现 `submit(Executor& ex)` 提交到 executor 执行
- [x] 实现 `wait()` 等待所有任务完成
- [x] 按拓扑序调度，DAG 结构保证无循环依赖

### 3.3 submit_task_graph

- [ ] 实现 `submit_task_graph(Executor& ex, TaskGraph& graph)` API
- [ ] 内部调用 `Executor::submit` 将每个节点作为任务提交
- [ ] 依赖通过 future 或自定义调度逻辑实现

---

## Phase 4：FrameData 与双缓冲（1 周）

### 4.1 FrameData

- [ ] 实现 `FrameData<T>` 模板类
- [ ] 实现 `write_buffer()` 获取当前写入缓冲区
- [ ] 实现 `read_buffer()` 获取当前只读快照（上一帧或已提交）
- [ ] 实现 `end_frame()` 帧末交换缓冲区
- [ ] 双缓冲或三缓冲：当前帧写入，上一帧只读

### 4.2 SwapBuffer

- [ ] 实现 `SwapBuffer<T, N>` 模板（N=2 为双缓冲，N=3 为三缓冲）
- [ ] 实现 `current_for_writer()` 和 `current_for_reader()`
- [ ] 实现 `swap()` 由单一协调者调用
- [ ] `end_frame` 使用原子或简短临界区交换指针

### 4.3 引擎集成

- [ ] 与引擎主循环集成示例
- [ ] 帧边界同步：主线程帧末 swap，worker 使用稳定快照

---

## Phase 5：渲染引擎集成（2 周）

### 5.1 RenderTaskScheduler 扩展

- [ ] 实现 `RenderTaskScheduler` Facade（基于 executor）
- [ ] 实现 `SubmitRenderTask(Func&& task, dependencies)` → executor.submit
- [ ] 实现 `SubmitSystemUpdate(System* system, deps)` → 构建 System 依赖图后 submit
- [ ] 实现 `LoadResourceAsync<Resource>(path)` → executor.submit 返回 Future
- [ ] 实现 `WaitAll()` 等待所有任务
- [ ] 实现 `ParallelRecordCommands(passes)` 按 Pass DAG 拓扑序分组，无依赖 Pass 并行录制

### 5.2 新增扩展接口

- [ ] 实现 `GetResourceLoadedChannel()` 返回 `TaskChannel<ResourceLoadedEvent>*`
- [ ] 实现 `SubmitTaskGraph(TaskGraph& graph)`
- [ ] 实现 `GetVisibleObjectsFrameData()` 返回 `FrameData<VisibleObjectList>*`

### 5.3 资源管理集成

- [ ] ResourceLoader 内部 `LoadAsync` 使用 `resource_loaded_channel_->try_send()`
- [ ] ResourceManager 每帧 `ProcessLoadedResources()` 调用 `channel_->try_recv()`
- [ ] 实现 `ResourceLoadedEvent` 结构（path, resource handle）

### 5.4 ECS 并行系统集成

- [ ] 只读组件：多系统并行读取，无需额外同步
- [ ] 写组件：通过任务依赖保证同一实体不被多个系统并发写
- [ ] 系统间数据：通过 TaskData 输出槽或 FrameData 共享当前帧快照
- [ ] EntityManager 根据 `System::GetDependencies()` 构建 DAG 后提交

### 5.5 Render Graph 并行录制集成

- [ ] 每个 Pass 对应一个任务，依赖由 Render Graph 的 DAG 决定
- [ ] 每个 Pass 写自己的 Command List，无共享写
- [ ] 最后提交阶段由主线程或单一任务汇总
- [ ] 通过 `std::future` 收集各 Pass 的 CommandList*
- [ ] RDI：`BeginCommandList(threadIndex)` 每线程独立 CommandPool

### 5.6 性能与验证

- [ ] 性能测试与调优
- [ ] 验证无死锁、无数据竞争

---

## 通信原语 API 汇总

### 对外 API（executor 命名空间）

| API | 说明 |
|-----|------|
| `make_channel<T, Cap>()` | 创建 SPSC 通道 |
| `submit_task_graph(Executor&, TaskGraph&)` | 提交带依赖的任务图 |
| `async_load<T>(Executor&, std::function<T()>)` | 异步加载并返回 Future |
| `make_frame_data<T>()` | 帧数据管理 |

### 可选编译选项

- [ ] `EXECUTOR_ENABLE_CHANNELS` 控制是否编译通道
- [ ] `EXECUTOR_ENABLE_TASK_GRAPH` 控制任务图扩展

---

## 依赖与集成

### executor 库集成

- [ ] 通过 `find_package(executor)` 或 `add_subdirectory` 集成
- [ ] 扩展作为独立头文件/库，通过额外 target 提供
- [ ] `TaskGraph::submit` 内部调用 `Executor::submit`

### 引擎初始化顺序

- [ ] scheduler → sceneManager → entityManager(scheduler, sceneManager) / resourceManager(scheduler) / renderGraph
- [ ] EntityManager 持有 SceneManager 指针，供 SceneNodeRef 解析

---

## 参考文档

- [executor_layer_design.md](../docs/design/executor_layer_design.md) - 线程间数据通信设计
- [rendering_engine_design.md](../docs/design/rendering_engine_design.md) - 渲染引擎架构
- [rendering_engine_code_examples.md](../docs/design/rendering_engine_code_examples.md) - 代码示例
- [executor/README.md](../docs/executor/README.md) - executor 库说明
