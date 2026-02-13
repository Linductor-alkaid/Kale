// Kale 执行器层 - TaskGraph
// 带 DAG 依赖的任务图，按拓扑序提交到 executor 执行

#pragma once

#include <kale_executor/task_data_manager.hpp>

#include <executor/executor.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <future>
#include <queue>
#include <stdexcept>
#include <vector>

namespace kale::executor {

/// 任务执行上下文，供 TaskFunc 内访问当前任务句柄与数据槽
struct TaskContext {
    TaskHandle task_handle = kInvalidTaskHandle;
    TaskDataManager* data_mgr = nullptr;

    /// 只读访问输入槽（由依赖任务写入）
    const void* get_input(DataSlotHandle h) const {
        return data_mgr ? data_mgr->get_slot(h) : nullptr;
    }
    /// 可写访问输出槽（任务执行期间独占写入）
    void* get_output(DataSlotHandle h) {
        return data_mgr ? data_mgr->get_slot(h) : nullptr;
    }
};

/// 任务函数类型：接收 TaskContext，无返回值
using TaskFunc = std::function<void(const TaskContext&)>;

/// 任务图：DAG 任务调度，按拓扑序提交到 executor，支持 wait() 等待全部完成
class TaskGraph {
public:
    TaskGraph() = default;
    ~TaskGraph() = default;

    TaskGraph(const TaskGraph&) = delete;
    TaskGraph& operator=(const TaskGraph&) = delete;

    /// 可选：设置任务数据管理器，供 TaskContext::get_input/get_output 使用
    void set_task_data_manager(TaskDataManager* mgr) { data_mgr_ = mgr; }
    TaskDataManager* get_task_data_manager() const { return data_mgr_; }

    /// 添加任务节点，dependencies 为必须先完成的任务句柄集合
    /// 返回本任务的句柄，供后续 add_task / add_task_with_data 的依赖引用
    TaskHandle add_task(TaskFunc func,
                        std::vector<TaskHandle> dependencies = {});

    /// 添加带“被依赖”声明的任务：本任务依赖 deps，且 dependents 中的任务依赖本任务
    /// 即会为每个 dependents 中的句柄添加“依赖本任务”的边
    TaskHandle add_task_with_data(TaskFunc func,
                                  std::vector<TaskHandle> deps,
                                  std::vector<TaskHandle> dependents);

    /// 按拓扑序提交到 executor 执行；无节点时直接返回
    void submit(::executor::Executor& ex);

    /// 等待当前 submit 产生的所有任务完成；未调用 submit 或 submit 后已 wait 过则无效果
    void wait();

    /// 清空所有节点与状态，便于复用图
    void clear();

    /// 是否已提交且尚未 wait 完成（可用于调试）
    bool is_submitted() const { return submitted_; }

private:
    struct Node {
        TaskFunc func;
        std::vector<TaskHandle> dependencies;
    };

    /// 拓扑排序（Kahn），返回节点下标序；若存在环则返回空
    std::vector<size_t> build_topological_order() const;

    std::vector<Node> nodes_;
    TaskHandle next_handle_ = 1;
    TaskDataManager* data_mgr_ = nullptr;

    /// submit 时填写的每节点 shared_future，用于 wait
    std::vector<std::shared_future<void>> futures_;
    bool submitted_ = false;
};

// -----------------------------------------------------------------------------
// 实现
// -----------------------------------------------------------------------------

inline TaskHandle TaskGraph::add_task(TaskFunc func,
                                      std::vector<TaskHandle> dependencies) {
    TaskHandle h = next_handle_++;
    nodes_.push_back(Node{std::move(func), std::move(dependencies)});
    return h;
}

inline TaskHandle TaskGraph::add_task_with_data(TaskFunc func,
                                               std::vector<TaskHandle> deps,
                                               std::vector<TaskHandle> dependents) {
    TaskHandle h = add_task(std::move(func), std::move(deps));
    for (TaskHandle d : dependents) {
        if (d == kInvalidTaskHandle || d == 0) continue;
        size_t idx = static_cast<size_t>(d - 1);
        if (idx < nodes_.size())
            nodes_[idx].dependencies.push_back(h);
    }
    return h;
}

inline std::vector<size_t> TaskGraph::build_topological_order() const {
    const size_t n = nodes_.size();
    if (n == 0) return {};

    std::vector<int> in_degree(n, 0);
    for (size_t i = 0; i < n; ++i) {
        for (TaskHandle dep : nodes_[i].dependencies) {
            if (dep == kInvalidTaskHandle || dep == 0) continue;
            size_t di = static_cast<size_t>(dep - 1);
            if (di < n) in_degree[i]++;
        }
    }

    std::queue<size_t> q;
    for (size_t i = 0; i < n; ++i)
        if (in_degree[i] == 0) q.push(i);

    std::vector<size_t> order;
    order.reserve(n);
    while (!q.empty()) {
        size_t u = q.front();
        q.pop();
        order.push_back(u);
        TaskHandle hu = static_cast<TaskHandle>(u + 1);
        for (size_t v = 0; v < n; ++v) {
            if (v == u) continue;
            const auto& deps = nodes_[v].dependencies;
            if (std::find(deps.begin(), deps.end(), hu) != deps.end()) {
                if (--in_degree[v] == 0) q.push(v);
            }
        }
    }
    if (order.size() != n)
        return {};  // 存在环
    return order;
}

inline void TaskGraph::submit(::executor::Executor& ex) {
    futures_.clear();
    submitted_ = false;

    std::vector<size_t> order = build_topological_order();
    if (order.empty() && !nodes_.empty())
        throw std::runtime_error("TaskGraph: cycle detected");
    if (order.empty()) return;

    const size_t n = nodes_.size();
    futures_.resize(n);

    for (size_t idx : order) {
        const Node& node = nodes_[idx];
        TaskHandle my_handle = static_cast<TaskHandle>(idx + 1);
        TaskContext ctx;
        ctx.task_handle = my_handle;
        ctx.data_mgr = data_mgr_;

        std::vector<std::shared_future<void>> dep_futures;
        dep_futures.reserve(node.dependencies.size());
        for (TaskHandle dep : node.dependencies) {
            if (dep == kInvalidTaskHandle || dep == 0) continue;
            size_t di = static_cast<size_t>(dep - 1);
            if (di < n && futures_[di].valid())
                dep_futures.push_back(futures_[di]);
        }

        TaskFunc func = node.func;
        auto run = [func, ctx, dep_futures]() {
            for (auto& f : dep_futures) f.wait();
            func(ctx);
        };

        std::future<void> f = ex.submit(std::move(run));
        futures_[idx] = f.share();
    }
    submitted_ = true;
}

inline void TaskGraph::wait() {
    if (!submitted_) return;
    for (auto& f : futures_)
        if (f.valid()) f.wait();
    submitted_ = false;
}

inline void TaskGraph::clear() {
    nodes_.clear();
    next_handle_ = 1;
    futures_.clear();
    submitted_ = false;
}

}  // namespace kale::executor
