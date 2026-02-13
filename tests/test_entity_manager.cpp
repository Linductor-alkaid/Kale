/**
 * @file test_entity_manager.cpp
 * @brief phase7-7.3 EntityManager 单元测试
 *
 * 覆盖：CreateEntity/DestroyEntity/IsAlive、AddComponent/GetComponent/HasComponent/RemoveComponent、
 * EntitiesWith、RegisterSystem、Update（含依赖 DAG）、SetSceneManager/GetSceneManager、
 * OnEntityCreated/OnEntityDestroyed。
 */

#include <kale_scene/entity_manager.hpp>
#include <kale_scene/entity.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
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

struct CompA { int x = 0; };
struct CompB { float y = 0.f; };
struct CompC { std::string z; };

class CountingSystem : public kale::scene::System {
public:
    int created = 0;
    int destroyed = 0;
    void Update(float /*deltaTime*/, kale::scene::EntityManager& /*em*/) override {}
    void OnEntityCreated(kale::scene::Entity /*e*/) override { created++; }
    void OnEntityDestroyed(kale::scene::Entity /*e*/) override { destroyed++; }
};

class SimpleSystem : public kale::scene::System {
public:
    int updateCount = 0;
    void Update(float deltaTime, kale::scene::EntityManager& em) override {
        (void)deltaTime;
        auto entities = em.EntitiesWith<CompA>();
        updateCount += static_cast<int>(entities.size());
    }
};

class DependentSystem : public kale::scene::System {
public:
    std::type_index DepType;
    explicit DependentSystem(std::type_index dep) : DepType(dep) {}
    void Update(float /*deltaTime*/, kale::scene::EntityManager& /*em*/) override {}
    std::vector<std::type_index> GetDependencies() const override { return {DepType}; }
};

}  // namespace

int main() {
    using namespace kale::scene;

    EntityManager em(nullptr, nullptr);
    TEST_CHECK(em.GetSceneManager() == nullptr);

    // --- CreateEntity / IsAlive ---
    Entity e1 = em.CreateEntity();
    TEST_CHECK(e1.IsValid());
    TEST_CHECK(em.IsAlive(e1));

    Entity e2 = em.CreateEntity();
    TEST_CHECK(e2.IsValid());
    TEST_CHECK(e1.id != e2.id || e1.generation != e2.generation);

    // --- AddComponent / GetComponent / HasComponent ---
    em.AddComponent<CompA>(e1, 10);
    TEST_CHECK(em.HasComponent<CompA>(e1));
    TEST_CHECK(em.GetComponent<CompA>(e1) != nullptr);
    TEST_CHECK(em.GetComponent<CompA>(e1)->x == 10);

    em.AddComponent<CompB>(e1, 3.14f);
    TEST_CHECK(em.HasComponent<CompB>(e1));
    TEST_CHECK(em.GetComponent<CompB>(e1)->y == 3.14f);

    TEST_CHECK(!em.HasComponent<CompC>(e1));
    TEST_CHECK(em.GetComponent<CompC>(e1) == nullptr);

    em.AddComponent<CompA>(e2, 20);
    em.AddComponent<CompB>(e2, 2.5f);
    em.AddComponent<CompC>(e2, "hello");

    // --- EntitiesWith ---
    auto withA = em.EntitiesWith<CompA>();
    TEST_CHECK(withA.size() == 2u);
    auto withAB = em.EntitiesWith<CompA, CompB>();
    TEST_CHECK(withAB.size() == 2u);
    auto withABC = em.EntitiesWith<CompA, CompB, CompC>();
    TEST_CHECK(withABC.size() == 1u);
    TEST_CHECK(withABC[0].id == e2.id);

    // --- RemoveComponent ---
    em.RemoveComponent(e1, std::type_index(typeid(CompA)));
    TEST_CHECK(!em.HasComponent<CompA>(e1));
    TEST_CHECK(em.HasComponent<CompB>(e1));
    auto withA2 = em.EntitiesWith<CompA>();
    TEST_CHECK(withA2.size() == 1u);

    // --- DestroyEntity / IsAlive ---
    em.DestroyEntity(e1);
    TEST_CHECK(!em.IsAlive(e1));
    TEST_CHECK(em.GetComponent<CompB>(e1) == nullptr);
    auto withB = em.EntitiesWith<CompB>();
    TEST_CHECK(withB.size() == 1u);

    // --- 复用 id 后旧 handle 无效 ---
    Entity e3 = em.CreateEntity();
    TEST_CHECK(em.IsAlive(e3));
    TEST_CHECK(!em.IsAlive(e1));
    em.AddComponent<CompA>(e3, 30);
    TEST_CHECK(em.EntitiesWith<CompA>().size() == 2u);

    // --- RegisterSystem / OnEntityCreated / OnEntityDestroyed ---
    auto counting = std::make_unique<CountingSystem>();
    CountingSystem* ptrCounting = counting.get();
    em.RegisterSystem(std::move(counting));
    Entity e4 = em.CreateEntity();
    Entity e5 = em.CreateEntity();
    TEST_CHECK(ptrCounting->created == 2);
    em.DestroyEntity(e4);
    TEST_CHECK(ptrCounting->destroyed == 1);
    em.DestroyEntity(e5);
    TEST_CHECK(ptrCounting->destroyed == 2);

    // --- Update 与 System 执行顺序 ---
    auto simple = std::make_unique<SimpleSystem>();
    SimpleSystem* ptrSimple = simple.get();
    em.RegisterSystem(std::move(simple));
    em.Update(0.016f);
    TEST_CHECK(ptrSimple->updateCount == 2);  // e2, e3 有 CompA
    em.Update(0.016f);
    TEST_CHECK(ptrSimple->updateCount == 4);

    // --- 依赖 DAG：DependentSystem 依赖 SimpleSystem，Update 仍按拓扑序调用 ---
    EntityManager em2(nullptr, nullptr);
    auto first = std::make_unique<SimpleSystem>();
    SimpleSystem* pFirst = first.get();
    em2.RegisterSystem(std::move(first));
    auto second = std::make_unique<DependentSystem>(std::type_index(typeid(SimpleSystem)));
    em2.RegisterSystem(std::move(second));
    Entity e6 = em2.CreateEntity();
    em2.AddComponent<CompA>(e6, 1);
    em2.Update(0.016f);
    TEST_CHECK(pFirst->updateCount == 1);

    std::cout << "test_entity_manager: all checks passed.\n";
    return 0;
}
