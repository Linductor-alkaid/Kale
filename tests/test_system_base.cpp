/**
 * @file test_system_base.cpp
 * @brief phase7-7.4 System 基类单元测试
 *
 * 覆盖：纯虚 Update(deltaTime, EntityManager&)、GetDependencies() 默认空、
 * OnEntityCreated/OnEntityDestroyed 可选回调、继承与虚表。
 */

#include <kale_scene/entity_manager.hpp>
#include <kale_scene/entity.hpp>

#include <cstdlib>
#include <iostream>
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

struct DummyComp { int v = 0; };

/// 测试纯虚 Update 必须实现
class ConcreteSystem : public kale::scene::System {
public:
    int updates = 0;
    void Update(float deltaTime, kale::scene::EntityManager& em) override {
        (void)deltaTime;
        (void)em;
        updates++;
    }
};

/// 测试 GetDependencies() 默认返回空
class DefaultDepsSystem : public kale::scene::System {
public:
    void Update(float, kale::scene::EntityManager&) override {}
    // 不重写 GetDependencies，使用基类默认空
};

/// 测试 GetDependencies() 可返回非空
class WithDepsSystem : public kale::scene::System {
public:
    void Update(float, kale::scene::EntityManager&) override {}
    std::vector<std::type_index> GetDependencies() const override {
        return { std::type_index(typeid(ConcreteSystem)) };
    }
};

/// 测试 OnEntityCreated / OnEntityDestroyed 可选回调
class LifecycleSystem : public kale::scene::System {
public:
    int created = 0;
    int destroyed = 0;
    void Update(float, kale::scene::EntityManager&) override {}
    void OnEntityCreated(kale::scene::Entity) override { created++; }
    void OnEntityDestroyed(kale::scene::Entity) override { destroyed++; }
};

}  // namespace

int main() {
    using namespace kale::scene;

    // --- 继承与纯虚 Update ---
    EntityManager em(nullptr, nullptr);
    auto concrete = std::make_unique<ConcreteSystem>();
    ConcreteSystem* pConcrete = concrete.get();
    em.RegisterSystem(std::move(concrete));
    em.Update(0.016f);
    em.Update(0.016f);
    TEST_CHECK(pConcrete->updates == 2);

    // --- GetDependencies() 默认空 ---
    auto defaultDeps = std::make_unique<DefaultDepsSystem>();
    em.RegisterSystem(std::move(defaultDeps));
    em.Update(0.016f);
    // 无依赖，拓扑序任意；仅验证不崩溃

    // --- GetDependencies() 返回非空（DAG 由 EntityManager 处理）---
    EntityManager em2(nullptr, nullptr);
    auto first = std::make_unique<ConcreteSystem>();
    em2.RegisterSystem(std::move(first));
    auto second = std::make_unique<WithDepsSystem>();
    em2.RegisterSystem(std::move(second));
    em2.Update(0.016f);
    // 仅验证注册与执行不崩溃

    // --- OnEntityCreated / OnEntityDestroyed ---
    EntityManager em3(nullptr, nullptr);
    auto lifecycle = std::make_unique<LifecycleSystem>();
    LifecycleSystem* pLife = lifecycle.get();
    em3.RegisterSystem(std::move(lifecycle));
    kale::scene::Entity a = em3.CreateEntity();
    kale::scene::Entity b = em3.CreateEntity();
    TEST_CHECK(pLife->created == 2);
    TEST_CHECK(pLife->destroyed == 0);
    em3.DestroyEntity(a);
    TEST_CHECK(pLife->destroyed == 1);
    em3.DestroyEntity(b);
    TEST_CHECK(pLife->destroyed == 2);

    std::cout << "test_system_base: all checks passed.\n";
    return 0;
}
