/**
 * @file test_ecs_parallel_system_integration.cpp
 * @brief phase9-9.9 ECS 并行系统集成单元测试
 *
 * 覆盖：
 * - EntityManager 根据 GetDependencies() 构建 DAG 后按拓扑序提交，依赖系统在依赖完成后执行
 * - 无依赖系统可并行（同层多任务提交），有依赖系统等待 futures 后执行
 * - 系统间数据通过 FrameData 共享：写系统写入 write_buffer() 并 end_frame()，读系统（依赖写系统）从 read_buffer() 读取
 */

#include <kale_scene/entity_manager.hpp>
#include <kale_scene/entity.hpp>
#include <kale_executor/render_task_scheduler.hpp>
#include <kale_executor/frame_data.hpp>

#include <executor/executor.hpp>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <typeindex>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

namespace {

using namespace kale::scene;
using namespace kale::executor;

/// 无依赖系统 A：写入标志
class SystemA : public kale::scene::System {
public:
    std::atomic<int>* out = nullptr;
    void Update(float /*deltaTime*/, EntityManager& /*em*/) override {
        if (out) out->store(1);
    }
};

/// 无依赖系统 B：写入另一标志
class SystemB : public kale::scene::System {
public:
    std::atomic<int>* out = nullptr;
    void Update(float /*deltaTime*/, EntityManager& /*em*/) override {
        if (out) out->store(2);
    }
};

/// 依赖 A 和 B：读取 A、B 的结果，验证 DAG 顺序（先 A、B 再 C）
class SystemC : public kale::scene::System {
public:
    std::atomic<int>* a_val = nullptr;
    std::atomic<int>* b_val = nullptr;
    int seen_a = 0, seen_b = 0;
    void Update(float /*deltaTime*/, EntityManager& /*em*/) override {
        if (a_val) seen_a = a_val->load();
        if (b_val) seen_b = b_val->load();
    }
    std::vector<std::type_index> GetDependencies() const override {
        return {std::type_index(typeid(SystemA)), std::type_index(typeid(SystemB))};
    }
};

/// 写系统：向 FrameData 写入一帧数据并 end_frame()
class FrameDataWriterSystem : public kale::scene::System {
public:
    explicit FrameDataWriterSystem(FrameData<VisibleObjectList>* fd) : fd_(fd) {}
    void Update(float /*deltaTime*/, EntityManager& /*em*/) override {
        if (fd_) {
            fd_->write_buffer().nodes.push_back(reinterpret_cast<void*>(0x1234u));
            fd_->end_frame();
        }
    }
private:
    FrameData<VisibleObjectList>* fd_ = nullptr;
};

/// 读系统：依赖 FrameDataWriterSystem，从 read_buffer() 读取
class FrameDataReaderSystem : public kale::scene::System {
public:
    explicit FrameDataReaderSystem(FrameData<VisibleObjectList>* fd) : fd_(fd) {}
    void Update(float /*deltaTime*/, EntityManager& /*em*/) override {
        if (fd_) {
            const auto& list = fd_->read_buffer();
            read_count_ = list.nodes.size();
            if (!list.nodes.empty())
                last_node_ = list.nodes.back();
        }
    }
    std::vector<std::type_index> GetDependencies() const override {
        return {std::type_index(typeid(FrameDataWriterSystem))};
    }
    size_t read_count() const { return read_count_; }
    void* last_node() const { return last_node_; }
private:
    FrameData<VisibleObjectList>* fd_ = nullptr;
    size_t read_count_ = 0;
    void* last_node_ = nullptr;
};

}  // namespace

int main() {
    using namespace kale::scene;

    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    // --- 1. DAG 顺序：A、B 无依赖，C 依赖 A 和 B ---
    std::atomic<int> a_done{0}, b_done{0};
    auto sysA = std::make_unique<SystemA>();
    sysA->out = &a_done;
    auto sysB = std::make_unique<SystemB>();
    sysB->out = &b_done;
    auto sysC = std::make_unique<SystemC>();
    sysC->a_val = &a_done;
    sysC->b_val = &b_done;
    SystemC* pC = sysC.get();

    EntityManager em1(&sched, nullptr);
    em1.RegisterSystem(std::move(sysA));
    em1.RegisterSystem(std::move(sysB));
    em1.RegisterSystem(std::move(sysC));

    TEST_CHECK(a_done.load() == 0 && b_done.load() == 0);
    em1.Update(0.016f);
    TEST_CHECK(a_done.load() == 1);
    TEST_CHECK(b_done.load() == 2);
    TEST_CHECK(pC->seen_a == 1 && pC->seen_b == 2);

    // --- 2. 系统间数据：FrameData 写/读 ---
    kale::executor::FrameData<kale::executor::VisibleObjectList>* fd =
        sched.GetVisibleObjectsFrameData();
    TEST_CHECK(fd != nullptr);

    auto writer = std::make_unique<FrameDataWriterSystem>(fd);
    auto reader = std::make_unique<FrameDataReaderSystem>(fd);
    FrameDataReaderSystem* pReader = reader.get();

    EntityManager em2(&sched, nullptr);
    em2.RegisterSystem(std::move(writer));
    em2.RegisterSystem(std::move(reader));

    em2.Update(0.016f);
    TEST_CHECK(pReader->read_count() == 1u);
    TEST_CHECK(pReader->last_node() == reinterpret_cast<void*>(0x1234u));

    ex.shutdown(true);

    std::cout << "test_ecs_parallel_system_integration: all checks passed.\n";
    return 0;
}
