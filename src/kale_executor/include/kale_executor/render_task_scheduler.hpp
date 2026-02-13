// Kale 执行器层 - RenderTaskScheduler
// 基于 executor 的 Facade：SubmitRenderTask、SubmitSystemUpdate、LoadResourceAsync、WaitAll、ParallelRecordCommands、扩展接口

#pragma once

#include <kale_executor/executor_future.hpp>
#include <kale_executor/frame_data.hpp>
#include <kale_executor/task_channel.hpp>
#include <kale_executor/task_graph.hpp>

#include <executor/executor.hpp>

#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <typeindex>
#include <vector>

namespace kale::executor {

/// 资源加载完成事件：供 LoadAsync 发送、ResourceManager 主线程 ProcessLoadedResources try_recv 消费
struct ResourceLoadedEvent {
    std::string path;
    uint64_t resource_handle_id = 0;
    std::type_index type_id{typeid(void)};
};

/// 可见物体列表：由 CullScene 等写入 write_buffer()，渲染管线从 read_buffer() 读取
struct VisibleObjectList {
    std::vector<void*> nodes;
};

/// 系统基类：供 SubmitSystemUpdate 与 ECS 层使用，依赖由 GetDependencies() 声明
struct System {
    virtual ~System() = default;
    virtual void Update(float deltaTime) = 0;
    /// 返回本系统依赖的系统类型（type_index），用于构建 DAG；默认无依赖
    virtual std::vector<std::type_index> GetDependencies() const { return {}; }
};

/// 渲染任务调度器 Facade：封装 executor，提供渲染管线所需的提交与等待接口
class RenderTaskScheduler {
public:
    explicit RenderTaskScheduler(::executor::Executor* ex) : ex_(ex) {}

    /// 获取底层 executor（只读）
    ::executor::Executor* GetExecutor() const { return ex_; }

    /// 提交渲染任务；dependencies 为空则立即可运行，否则先等待再执行 task
    /// 任务会被加入待等待列表，WaitAll() 可等待本批任务；返回本任务的 shared_future 供依赖链使用
    template <typename Func>
    std::shared_future<void> SubmitRenderTask(
        Func&& task,
        std::vector<std::shared_future<void>> dependencies = {});

    /// 提交系统更新：等待 deps 后调用 system->Update(deltaTime)
    void SubmitSystemUpdate(System* system,
                            float deltaTime,
                            std::vector<std::shared_future<void>> deps = {});

    /// 异步加载：在 executor 中执行 loader，返回 Future<R>（等价于 async_load）
    template <typename R>
    ExecutorFuture<R> LoadResourceAsync(std::function<R()> loader);

    /// 等待当前已提交且被跟踪的所有任务完成，并清空跟踪列表
    void WaitAll();

    /// 按 Pass DAG 拓扑序分组：无依赖的 Pass 并行录制，有依赖的按层级串行
    /// recordFuncs[i] 为第 i 个 Pass 的录制函数；dependencies[i] 为 Pass i 依赖的 Pass 下标
    void ParallelRecordCommands(
        std::vector<std::function<void()>> recordFuncs,
        std::vector<std::vector<size_t>> dependencies);

    /// 提交任务图到底层 executor（等价于 submit_task_graph(*ex_, graph)）
    void SubmitTaskGraph(TaskGraph& graph);

    /// 获取资源加载完成通道：加载线程 try_send(ResourceLoadedEvent)，主线程每帧 try_recv
    TaskChannel<ResourceLoadedEvent, 32>* GetResourceLoadedChannel();

    /// 获取可见物体列表帧数据：写者写入 write_buffer()，读者读 read_buffer()，帧末 end_frame()
    FrameData<VisibleObjectList>* GetVisibleObjectsFrameData();

private:
    ::executor::Executor* ex_ = nullptr;
    std::vector<std::shared_future<void>> pending_;
    std::mutex pending_mutex_;
    std::unique_ptr<TaskChannel<ResourceLoadedEvent, 32>> resource_loaded_channel_;
    std::unique_ptr<FrameData<VisibleObjectList>> visible_objects_frame_data_;
};

// -----------------------------------------------------------------------------
// 实现
// -----------------------------------------------------------------------------

template <typename Func>
std::shared_future<void> RenderTaskScheduler::SubmitRenderTask(
    Func&& task,
    std::vector<std::shared_future<void>> dependencies) {
    if (!ex_) return std::shared_future<void>();

    auto run = [task = std::forward<Func>(task),
                deps = std::move(dependencies)]() {
        for (auto& f : deps)
            if (f.valid()) f.wait();
        task();
    };

    std::future<void> f = ex_->submit(std::move(run));
    std::shared_future<void> sf = f.share();
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_.push_back(sf);
    return sf;
}

inline void RenderTaskScheduler::SubmitSystemUpdate(
    System* system,
    float deltaTime,
    std::vector<std::shared_future<void>> deps) {
    if (!ex_ || !system) return;

    System* s = system;
    float dt = deltaTime;
    auto run = [s, dt, deps = std::move(deps)]() {
        for (auto& f : deps)
            if (f.valid()) f.wait();
        s->Update(dt);
    };

    std::future<void> f = ex_->submit(std::move(run));
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_.push_back(f.share());
}

template <typename R>
ExecutorFuture<R> RenderTaskScheduler::LoadResourceAsync(std::function<R()> loader) {
    if (!ex_ || !loader) return ExecutorFuture<R>{};
    return async_load<R>(*ex_, std::move(loader));
}

inline void RenderTaskScheduler::WaitAll() {
    std::vector<std::shared_future<void>> local;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        local = std::move(pending_);
        pending_.clear();
    }
    for (auto& f : local)
        if (f.valid()) f.wait();
}

inline void RenderTaskScheduler::ParallelRecordCommands(
    std::vector<std::function<void()>> recordFuncs,
    std::vector<std::vector<size_t>> dependencies) {
    if (!ex_ || recordFuncs.empty()) return;

    const size_t n = recordFuncs.size();
    if (dependencies.size() != n)
        dependencies.resize(n);

    // 拓扑排序：dependencies[i] = Pass i 依赖的 Pass 下标，即 j 必须先于 i 执行
    std::vector<int> in_degree(n, 0);
    for (size_t i = 0; i < n; ++i)
        for (size_t j : dependencies[i])
            if (j < n) in_degree[i]++;

    std::vector<size_t> level;
    for (size_t i = 0; i < n; ++i)
        if (in_degree[i] == 0) level.push_back(i);

    std::vector<std::vector<size_t>> levels;
    while (!level.empty()) {
        levels.push_back(level);
        std::vector<size_t> next;
        for (size_t idx : level) {
            for (size_t j = 0; j < n; ++j) {
                for (size_t d : dependencies[j])
                    if (d == idx) {
                        in_degree[j]--;
                        if (in_degree[j] == 0) next.push_back(j);
                        break;
                    }
            }
        }
        level = std::move(next);
    }

    for (const auto& group : levels) {
        std::vector<std::shared_future<void>> futures;
        futures.reserve(group.size());
        for (size_t idx : group) {
            std::function<void()> fn = recordFuncs[idx];
            if (fn)
                futures.push_back(ex_->submit(std::move(fn)).share());
        }
        for (auto& f : futures)
            if (f.valid()) f.wait();
    }
}

inline void RenderTaskScheduler::SubmitTaskGraph(TaskGraph& graph) {
    if (ex_) submit_task_graph(*ex_, graph);
}

inline TaskChannel<ResourceLoadedEvent, 32>* RenderTaskScheduler::GetResourceLoadedChannel() {
    if (!resource_loaded_channel_)
        resource_loaded_channel_ = std::make_unique<TaskChannel<ResourceLoadedEvent, 32>>();
    return resource_loaded_channel_.get();
}

inline FrameData<VisibleObjectList>* RenderTaskScheduler::GetVisibleObjectsFrameData() {
    if (!visible_objects_frame_data_)
        visible_objects_frame_data_ = std::make_unique<FrameData<VisibleObjectList>>();
    return visible_objects_frame_data_.get();
}

}  // namespace kale::executor
