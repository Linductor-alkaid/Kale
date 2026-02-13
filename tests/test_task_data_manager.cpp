// TaskDataManager 与 DataSlot 单元测试

#include <kale_executor/task_data_manager.hpp>

#include <cstring>
#include <iostream>
#include <cstdlib>

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

static void test_data_slot_handle_invalid() {
    kale::executor::DataSlotHandle h = kale::executor::kInvalidDataSlotHandle;
    TEST_CHECK(!h.IsValid());
    TEST_CHECK(h.id == 0);
    TEST_CHECK(h.generation == 0);
}

static void test_allocate_get_release() {
    kale::executor::TaskDataManager mgr;
    const size_t size = 64;

    kale::executor::DataSlotHandle h = mgr.allocate_slot(size);
    TEST_CHECK(h.IsValid());
    TEST_CHECK(h.id != 0);
    TEST_CHECK(h.generation != 0);

    void* ptr = mgr.get_slot(h);
    TEST_CHECK(ptr != nullptr);

    // 写入并读出
    std::memset(ptr, 0xAB, size);
    const void* cptr = mgr.get_slot(h);
    TEST_CHECK(cptr == ptr);
    const unsigned char* bytes = static_cast<const unsigned char*>(cptr);
    for (size_t i = 0; i < size; ++i) TEST_CHECK(bytes[i] == 0xAB);

    mgr.release_slot(h);
    TEST_CHECK(mgr.get_slot(h) == nullptr);

    // 再次分配可能复用或新 id，至少能再分配
    kale::executor::DataSlotHandle h2 = mgr.allocate_slot(32);
    TEST_CHECK(h2.IsValid());
    TEST_CHECK(mgr.get_slot(h2) != nullptr);
    mgr.release_slot(h2);
}

static void test_allocate_zero_size() {
    kale::executor::TaskDataManager mgr;
    kale::executor::DataSlotHandle h = mgr.allocate_slot(0);
    TEST_CHECK(!h.IsValid());
    TEST_CHECK(h == kale::executor::kInvalidDataSlotHandle);
}

static void test_multiple_slots() {
    kale::executor::TaskDataManager mgr;
    std::vector<kale::executor::DataSlotHandle> handles;
    for (int i = 0; i < 10; ++i) {
        auto h = mgr.allocate_slot(8);
        TEST_CHECK(h.IsValid());
        void* p = mgr.get_slot(h);
        TEST_CHECK(p != nullptr);
        *static_cast<int*>(p) = i;
        handles.push_back(h);
    }
    for (int i = 0; i < 10; ++i) {
        void* p = mgr.get_slot(handles[i]);
        TEST_CHECK(p != nullptr);
        TEST_CHECK(*static_cast<int*>(p) == i);
    }
    for (auto& h : handles) mgr.release_slot(h);
    for (auto& h : handles) TEST_CHECK(mgr.get_slot(h) == nullptr);
}

static void test_bind_dependency() {
    kale::executor::TaskDataManager mgr;
    kale::executor::DataSlotHandle out_a = mgr.allocate_slot(4);
    kale::executor::DataSlotHandle in_b = mgr.allocate_slot(4);
    TEST_CHECK(out_a.IsValid() && in_b.IsValid());

    kale::executor::TaskHandle task_a = 1;
    kale::executor::TaskHandle task_b = 2;
    mgr.bind_dependency(task_a, out_a, task_b, in_b);

    const auto& edges = mgr.get_dependency_edges();
    TEST_CHECK(edges.size() == 1u);
    TEST_CHECK(edges[0].task_a == task_a);
    TEST_CHECK(edges[0].slot_a_out.id == out_a.id);
    TEST_CHECK(edges[0].task_b == task_b);
    TEST_CHECK(edges[0].slot_b_in.id == in_b.id);

    mgr.bind_dependency(3, mgr.allocate_slot(8), 4, mgr.allocate_slot(8));
    TEST_CHECK(edges.size() == 2u);

    mgr.release_slot(out_a);
    mgr.release_slot(in_b);
}

static void test_bind_dependency_invalid_ignored() {
    kale::executor::TaskDataManager mgr;
    kale::executor::DataSlotHandle valid = mgr.allocate_slot(4);
    const auto& edges = mgr.get_dependency_edges();
    size_t before = edges.size();

    mgr.bind_dependency(kale::executor::kInvalidTaskHandle, valid, 2, valid);
    mgr.bind_dependency(1, valid, kale::executor::kInvalidTaskHandle, valid);
    mgr.bind_dependency(1, kale::executor::kInvalidDataSlotHandle, 2, valid);
    mgr.bind_dependency(1, valid, 2, kale::executor::kInvalidDataSlotHandle);
    TEST_CHECK(mgr.get_dependency_edges().size() == before);

    mgr.release_slot(valid);
}

static void test_release_invalid_handle_no_crash() {
    kale::executor::TaskDataManager mgr;
    mgr.release_slot(kale::executor::kInvalidDataSlotHandle);
    mgr.release_slot({999, 1});
}

int main() {
    test_data_slot_handle_invalid();
    test_allocate_get_release();
    test_allocate_zero_size();
    test_multiple_slots();
    test_bind_dependency();
    test_bind_dependency_invalid_ignored();
    test_release_invalid_handle_no_crash();
    std::cout << "test_task_data_manager: all passed\n";
    return 0;
}
