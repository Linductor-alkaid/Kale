// Kale 执行器层 - 任务数据槽与 TaskDataManager
// 为任务图节点提供输入/输出数据槽，由管理器负责生命周期

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace kale::executor {

/// 数据槽句柄（不直接持有数据，避免复制）
struct DataSlotHandle {
    uint64_t id = 0;
    uint32_t generation = 0;

    bool IsValid() const { return id != 0; }
    bool operator==(const DataSlotHandle& o) const {
        return id == o.id && generation == o.generation;
    }
    bool operator!=(const DataSlotHandle& o) const { return !(*this == o); }
};

inline constexpr DataSlotHandle kInvalidDataSlotHandle = {0, 0};

/// 任务句柄（由 TaskGraph 等分配，此处仅作依赖绑定用）
using TaskHandle = uint64_t;

inline constexpr TaskHandle kInvalidTaskHandle = 0;

/// 依赖边：task_b 的 input_slot 依赖 task_a 的 output_slot
struct DataDependencyEdge {
    TaskHandle task_a = kInvalidTaskHandle;
    DataSlotHandle slot_a_out = kInvalidDataSlotHandle;
    TaskHandle task_b = kInvalidTaskHandle;
    DataSlotHandle slot_b_in = kInvalidDataSlotHandle;
};

/// 槽存储：内存块 + 代数（释放后递增以作废旧句柄）
struct SlotStorage {
    std::vector<std::uint8_t> data;
    uint32_t generation = 1;
    bool in_use = false;
};

/// 任务数据槽管理器
/// - 输入槽在任务开始前由执行器/调用方填充，任务执行期间只读
/// - 输出槽在任务执行期间独占写入，完成后生效
/// - bind_dependency 记录依赖边，供 TaskGraph 等按拓扑序填充输入
class TaskDataManager {
public:
    TaskDataManager() = default;
    ~TaskDataManager() = default;

    TaskDataManager(const TaskDataManager&) = delete;
    TaskDataManager& operator=(const TaskDataManager&) = delete;

    /// 分配 size_bytes 字节的数据槽，返回句柄；失败返回 kInvalidDataSlotHandle
    DataSlotHandle allocate_slot(std::size_t size_bytes);

    /// 获取槽内存指针；句柄无效或已释放返回 nullptr
    void* get_slot(DataSlotHandle h);

    /// 只读访问（语义约束，不强制）
    const void* get_slot(DataSlotHandle h) const {
        return const_cast<TaskDataManager*>(this)->get_slot(h);
    }

    /// 释放槽，此后该句柄失效（generation 递增）
    void release_slot(DataSlotHandle h);

    /// 绑定依赖：task_b 的 input 槽在调度前由 task_a 的 output 槽填充
    void bind_dependency(TaskHandle task_a, DataSlotHandle slot_a_out,
                         TaskHandle task_b, DataSlotHandle slot_b_in);

    /// 查询依赖边（供 TaskGraph 等使用）
    const std::vector<DataDependencyEdge>& get_dependency_edges() const {
        return dependency_edges_;
    }

private:
    std::vector<SlotStorage> slots_;
    uint64_t next_slot_id_ = 1;
    std::vector<DataDependencyEdge> dependency_edges_;
    mutable std::mutex mutex_;
};

// -----------------------------------------------------------------------------
// 实现
// -----------------------------------------------------------------------------

inline DataSlotHandle TaskDataManager::allocate_slot(std::size_t size_bytes) {
    if (size_bytes == 0) return kInvalidDataSlotHandle;
    std::lock_guard lock(mutex_);
    SlotStorage s;
    s.data.resize(size_bytes);
    s.generation = 1;
    s.in_use = true;
    uint64_t id = next_slot_id_++;
    slots_.push_back(std::move(s));
    // id 与 slots_ 下标对应：id 从 1 开始，下标为 id - 1
    DataSlotHandle h;
    h.id = id;
    h.generation = static_cast<uint32_t>(slots_.back().generation);
    return h;
}

inline void* TaskDataManager::get_slot(DataSlotHandle h) {
    if (!h.IsValid() || h.id > slots_.size()) return nullptr;
    std::lock_guard lock(mutex_);
    size_t idx = static_cast<size_t>(h.id - 1);
    if (idx >= slots_.size()) return nullptr;
    SlotStorage& s = slots_[idx];
    if (!s.in_use || s.generation != h.generation) return nullptr;
    return s.data.data();
}

inline void TaskDataManager::release_slot(DataSlotHandle h) {
    if (!h.IsValid() || h.id == 0 || h.id > slots_.size()) return;
    std::lock_guard lock(mutex_);
    size_t idx = static_cast<size_t>(h.id - 1);
    if (idx >= slots_.size()) return;
    SlotStorage& s = slots_[idx];
    if (s.generation != h.generation) return;
    s.in_use = false;
    s.generation++;
    // 可选：清空 data 以释放内存；此处保留容量便于复用
    s.data.clear();
}

inline void TaskDataManager::bind_dependency(TaskHandle task_a,
                                             DataSlotHandle slot_a_out,
                                             TaskHandle task_b,
                                             DataSlotHandle slot_b_in) {
    if (task_a == kInvalidTaskHandle || task_b == kInvalidTaskHandle) return;
    if (!slot_a_out.IsValid() || !slot_b_in.IsValid()) return;
    std::lock_guard lock(mutex_);
    dependency_edges_.push_back(
        {task_a, slot_a_out, task_b, slot_b_in});
}

}  // namespace kale::executor
