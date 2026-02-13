/**
 * @file test_entity_component_storage.cpp
 * @brief phase7-7.2 Entity 与 ComponentStorage 单元测试
 *
 * 覆盖：Entity::Null/IsValid、ComponentStorage Add/Remove/Get/Has、
 * 稠密存储与 Remove 交换、边界与无效实体。
 */

#include <kale_scene/entity.hpp>
#include <kale_scene/component_storage.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

int main() {
    using namespace kale::scene;

    // --- Entity ---
    TEST_CHECK(!Entity::Null().IsValid());
    TEST_CHECK(Entity::Null().id == 0 && Entity::Null().generation == 0);
    TEST_CHECK(!kNullEntity.IsValid());

    Entity e1{1, 0};
    TEST_CHECK(e1.IsValid());
    TEST_CHECK((e1 == Entity{1, 0}));
    TEST_CHECK(e1 != Entity::Null());
    TEST_CHECK((e1 != Entity{2, 0}));

    // --- ComponentStorage 基础 ---
    ComponentStorage<int> store;
    TEST_CHECK(store.Size() == 0);
    TEST_CHECK(!store.Has(e1));

    store.Add(e1, 42);
    TEST_CHECK(store.Size() == 1);
    TEST_CHECK(store.Has(e1));
    TEST_CHECK(store.Get(e1) == 42);

    store.Get(e1) = 100;
    TEST_CHECK(store.Get(e1) == 100);

    Entity e2{2, 0};
    store.Add(e2, 200);
    TEST_CHECK(store.Size() == 2);
    TEST_CHECK(store.Has(e2));
    TEST_CHECK(store.Get(e2) == 200);
    TEST_CHECK(store.Get(e1) == 100);

    // Add 覆盖
    store.Add(e1, 101);
    TEST_CHECK(store.Size() == 2);
    TEST_CHECK(store.Get(e1) == 101);

    // Remove 中间元素（交换与末尾）
    store.Remove(e1);
    TEST_CHECK(store.Size() == 1);
    TEST_CHECK(!store.Has(e1));
    TEST_CHECK(store.Has(e2));
    TEST_CHECK(store.Get(e2) == 200);

    store.Remove(e2);
    TEST_CHECK(store.Size() == 0);
    TEST_CHECK(!store.Has(e2));

    // 无效实体 Add 被忽略
    store.Add(Entity::Null(), 999);
    TEST_CHECK(store.Size() == 0);
    store.Add(kNullEntity, 999);
    TEST_CHECK(store.Size() == 0);

    // Remove 无效实体无操作
    store.Add(e1, 1);
    store.Remove(Entity::Null());
    store.Remove(Entity{0, 1});
    TEST_CHECK(store.Size() == 1 && store.Get(e1) == 1);

    // Has(无效实体) 为 false
    TEST_CHECK(!store.Has(Entity::Null()));

    // --- 非平凡类型 ---
    ComponentStorage<std::string> sstore;
    sstore.Add(e1, "hello");
    TEST_CHECK(sstore.Get(e1) == "hello");
    sstore.Add(e2, "world");
    TEST_CHECK(sstore.Size() == 2);
    sstore.Remove(e1);
    TEST_CHECK(sstore.Size() == 1);
    TEST_CHECK(sstore.Get(e2) == "world");

    // 迭代：EntityIdAt
    TEST_CHECK(sstore.EntityIdAt(0) == e2.id);

    // 多实体 Remove 顺序（保持稠密）
    ComponentStorage<int> dense;
    Entity a{10, 0}, b{20, 0}, c{30, 0};
    dense.Add(a, 10);
    dense.Add(b, 20);
    dense.Add(c, 30);
    dense.Remove(b);  // 移除中间，c 换到 b 的位置
    TEST_CHECK(dense.Size() == 2);
    TEST_CHECK(dense.Has(a) && dense.Has(c));
    TEST_CHECK(dense.Get(a) == 10 && dense.Get(c) == 30);
    dense.Remove(a);
    TEST_CHECK(dense.Size() == 1 && dense.Get(c) == 30);
    dense.Remove(c);
    TEST_CHECK(dense.Size() == 0);

    std::cout << "test_entity_component_storage: all checks passed.\n";
    return 0;
}
