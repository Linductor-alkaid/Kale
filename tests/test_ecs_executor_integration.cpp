/**
 * @file test_ecs_executor_integration.cpp
 * @brief phase7-7.5 ECS 与 executor 集成单元测试
 *
 * 覆盖：EntityManager::Update 通过 RenderTaskScheduler 按 DAG 提交系统任务、
 * 依赖的 System 先执行（拓扑序）、WaitAll 后全部完成。
 */

#include <kale_scene/entity_manager.hpp>
#include <kale_scene/entity.hpp>
#include <kale_executor/render_task_scheduler.hpp>

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

/// 先执行的系统：写入全局标志
class ProducerSystem : public kale::scene::System {
public:
    std::atomic<int>* flag = nullptr;
    void Update(float /*deltaTime*/, kale::scene::EntityManager& /*em*/) override {
        if (flag) flag->store(1);
    }
};

/// 依赖 ProducerSystem：读取标志，若顺序正确应为 1
class ConsumerSystem : public kale::scene::System {
public:
    std::atomic<int>* flag = nullptr;
    int seen = 0;
    void Update(float /*deltaTime*/, kale::scene::EntityManager& /*em*/) override {
        if (flag) seen = flag->load();
    }
    std::vector<std::type_index> GetDependencies() const override {
        return {std::type_index(typeid(ProducerSystem))};
    }
};

}  // namespace

int main() {
    using namespace kale::scene;

    ::executor::Executor ex;
    ex.initialize(::executor::ExecutorConfig{});
    kale::executor::RenderTaskScheduler sched(&ex);

    std::atomic<int> producerWrote{0};
    auto producer = std::make_unique<ProducerSystem>();
    producer->flag = &producerWrote;
    ProducerSystem* pProducer = producer.get();

    auto consumer = std::make_unique<ConsumerSystem>();
    consumer->flag = &producerWrote;
    ConsumerSystem* pConsumer = consumer.get();

    EntityManager em(&sched, nullptr);
    em.RegisterSystem(std::move(producer));
    em.RegisterSystem(std::move(consumer));

    TEST_CHECK(producerWrote.load() == 0);
    em.Update(0.016f);
    TEST_CHECK(producerWrote.load() == 1);
    TEST_CHECK(pConsumer->seen == 1);

    em.Update(0.016f);
    TEST_CHECK(pConsumer->seen == 1);

    ex.shutdown(true);

    std::cout << "test_ecs_executor_integration: all checks passed.\n";
    return 0;
}
