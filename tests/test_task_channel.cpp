// TaskChannel 单元测试与压力测试

#include <kale_executor/task_channel.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

// 模拟 ResourceLoadedEvent：生产场景使用的复杂类型
struct ResourceLoadedEvent {
    std::string path;
    void* handle = nullptr;

    ResourceLoadedEvent() = default;
    ResourceLoadedEvent(std::string p, void* h = nullptr)
        : path(std::move(p)), handle(h) {}
};

#define TEST_CHECK(cond)                                               \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__        \
                      << " " << #cond << std::endl;                    \
            std::exit(1);                                              \
        }                                                              \
    } while (0)

static void test_basic_send_recv() {
    kale::executor::TaskChannel<int, 64> ch;
    TEST_CHECK(ch.empty());
    TEST_CHECK(ch.size() == 0);

    TEST_CHECK(ch.try_send(42));
    TEST_CHECK(!ch.empty());
    TEST_CHECK(ch.size() == 1);

    int v = 0;
    TEST_CHECK(ch.try_recv(v));
    TEST_CHECK(v == 42);
    TEST_CHECK(ch.empty());
    TEST_CHECK(ch.size() == 0);

    TEST_CHECK(!ch.try_recv(v));
}

static void test_ordering() {
    kale::executor::TaskChannel<int, 64> ch;
    for (int i = 0; i < 32; ++i) ch.try_send(i);
    TEST_CHECK(ch.size() == 32);

    for (int i = 0; i < 32; ++i) {
        int v;
        TEST_CHECK(ch.try_recv(v));
        TEST_CHECK(v == i);
    }
    TEST_CHECK(ch.empty());
}

static void test_full() {
    kale::executor::TaskChannel<int, 8> ch;
    for (int i = 0; i < 8; ++i) TEST_CHECK(ch.try_send(i));
    TEST_CHECK(!ch.try_send(99));
    TEST_CHECK(ch.size() == 8);

    int v;
    TEST_CHECK(ch.try_recv(v));
    TEST_CHECK(v == 0);
    TEST_CHECK(ch.try_send(99));
}

static void test_spsc_threaded() {
    kale::executor::TaskChannel<int, 64> ch;
    constexpr int count = 10000;
    std::atomic<int> recv_count{0};

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            while (!ch.try_send(i))
                std::this_thread::yield();
        }
    });

    std::thread consumer([&] {
        for (int i = 0; i < count; ++i) {
            int v;
            while (!ch.try_recv(v))
                std::this_thread::yield();
            TEST_CHECK(v == i);
            recv_count++;
        }
    });

    producer.join();
    consumer.join();
    TEST_CHECK(recv_count.load() == count);
}

static void test_blocking_recv() {
    kale::executor::TaskChannel<int, 64> ch;
    int result = -1;

    std::thread consumer([&] {
        TEST_CHECK(ch.recv(result, std::chrono::milliseconds(500)));
        TEST_CHECK(result == 123);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ch.try_send(123);
    consumer.join();
}

static void test_blocking_send() {
    kale::executor::TaskChannel<int, 4> ch;
    ch.try_send(1);
    ch.try_send(2);
    ch.try_send(3);
    ch.try_send(4);

    std::thread producer([&] {
        TEST_CHECK(ch.send(5, std::chrono::milliseconds(500)));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    int v;
    ch.try_recv(v);
    TEST_CHECK(v == 1);
    producer.join();
    for (int i = 2; i <= 5; ++i) {
        TEST_CHECK(ch.try_recv(v));
        TEST_CHECK(v == i);
    }
    TEST_CHECK(ch.empty());
}

static void test_timeout() {
    kale::executor::TaskChannel<int, 64> ch;
    int v;
    TEST_CHECK(!ch.recv(v, std::chrono::milliseconds(10)));
}

static void test_make_channel() {
    auto ch = kale::executor::make_channel<int, 32>();
    TEST_CHECK(ch.capacity() >= 32);
    TEST_CHECK(ch.try_send(1));
    int v;
    TEST_CHECK(ch.try_recv(v));
    TEST_CHECK(v == 1);
}

static void test_stress() {
    kale::executor::TaskChannel<int, 128> ch;
    constexpr int rounds = 100;
    constexpr int count = 1000;

    std::thread producer([&] {
        for (int r = 0; r < rounds; ++r) {
            for (int i = 0; i < count; ++i) {
                while (!ch.try_send(r * count + i))
                    std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&] {
        for (int r = 0; r < rounds; ++r) {
            for (int i = 0; i < count; ++i) {
                int v;
                while (!ch.try_recv(v))
                    std::this_thread::yield();
                TEST_CHECK(v == r * count + i);
            }
        }
    });

    producer.join();
    consumer.join();
}

static void test_complex_type() {
    kale::executor::TaskChannel<ResourceLoadedEvent, 32> ch;
    TEST_CHECK(ch.try_send(ResourceLoadedEvent("textures/hero.png", (void*)0x123)));
    TEST_CHECK(ch.try_send(ResourceLoadedEvent("models/character.gltf", (void*)0x456)));

    ResourceLoadedEvent ev;
    TEST_CHECK(ch.try_recv(ev));
    TEST_CHECK(ev.path == "textures/hero.png");
    TEST_CHECK(ev.handle == (void*)0x123);

    TEST_CHECK(ch.try_recv(ev));
    TEST_CHECK(ev.path == "models/character.gltf");
    TEST_CHECK(ev.handle == (void*)0x456);

    TEST_CHECK(ch.empty());
}

static void test_complex_type_threaded() {
    kale::executor::TaskChannel<ResourceLoadedEvent, 64> ch;
    constexpr int count = 500;

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            std::string path = "res_" + std::to_string(i);
            while (!ch.try_send(ResourceLoadedEvent(path, (void*)(intptr_t)i)))
                std::this_thread::yield();
        }
    });

    std::thread consumer([&] {
        for (int i = 0; i < count; ++i) {
            ResourceLoadedEvent ev;
            while (!ch.try_recv(ev))
                std::this_thread::yield();
            TEST_CHECK(ev.path == "res_" + std::to_string(i));
            TEST_CHECK(ev.handle == (void*)(intptr_t)i);
        }
    });

    producer.join();
    consumer.join();
}

static void test_send_timeout() {
    kale::executor::TaskChannel<int, 4> ch;
    ch.try_send(1);
    ch.try_send(2);
    ch.try_send(3);
    ch.try_send(4);

    TEST_CHECK(!ch.send(5, std::chrono::milliseconds(50)));
    int v;
    ch.try_recv(v);
    TEST_CHECK(ch.send(5, std::chrono::milliseconds(100)));
}

static void test_send_timeout_zero() {
    kale::executor::TaskChannel<int, 2> ch;
    ch.try_send(1);
    ch.try_send(2);
    TEST_CHECK(!ch.send(3, std::chrono::milliseconds(0)));
}

static void test_recv_timeout_zero() {
    kale::executor::TaskChannel<int, 8> ch;
    int v = 999;
    TEST_CHECK(!ch.recv(v, std::chrono::milliseconds(0)));
    TEST_CHECK(v == 999);
}

static void test_capacity_small() {
    kale::executor::TaskChannel<int, 1> ch1;
    TEST_CHECK(ch1.capacity() >= 2);
    TEST_CHECK(ch1.try_send(1));
    TEST_CHECK(ch1.try_send(2));
    TEST_CHECK(!ch1.try_send(3));
    int v;
    TEST_CHECK(ch1.try_recv(v) && v == 1);
    TEST_CHECK(ch1.try_recv(v) && v == 2);
    TEST_CHECK(ch1.empty());

    kale::executor::TaskChannel<int, 2> ch2;
    ch2.try_send(10);
    ch2.try_send(20);
    TEST_CHECK(!ch2.try_send(30));
    TEST_CHECK(ch2.try_recv(v) && v == 10);
    TEST_CHECK(ch2.try_recv(v) && v == 20);
    TEST_CHECK(ch2.empty());
}

static void test_try_recv_preserves_out() {
    kale::executor::TaskChannel<int, 8> ch;
    int v = 42;
    TEST_CHECK(!ch.try_recv(v));
    TEST_CHECK(v == 42);
}

static void test_producer_faster() {
    kale::executor::TaskChannel<int, 64> ch;
    constexpr int count = 10000;
    std::atomic<int> produced{0};

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            while (!ch.try_send(i))
                std::this_thread::yield();
            produced.store(i + 1);
        }
    });

    std::thread consumer([&] {
        for (int i = 0; i < count; ++i) {
            int v;
            while (!ch.try_recv(v))
                std::this_thread::yield();
            TEST_CHECK(v == i);
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    producer.join();
    consumer.join();
    TEST_CHECK(produced.load() == count);
}

static void test_consumer_faster() {
    kale::executor::TaskChannel<int, 64> ch;
    constexpr int count = 10000;

    std::thread producer([&] {
        for (int i = 0; i < count; ++i) {
            while (!ch.try_send(i))
                std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    std::thread consumer([&] {
        for (int i = 0; i < count; ++i) {
            int v;
            while (!ch.try_recv(v))
                std::this_thread::yield();
            TEST_CHECK(v == i);
        }
    });

    producer.join();
    consumer.join();
}

static void test_blocking_multi_round() {
    kale::executor::TaskChannel<int, 2> ch;
    std::atomic<int> received{0};

    std::thread consumer([&] {
        for (int i = 0; i < 6; ++i) {
            int v;
            TEST_CHECK(ch.recv(v, std::chrono::milliseconds(200)));
            TEST_CHECK(v == i);
            received.store(i + 1);
        }
    });

    for (int i = 0; i < 6; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ch.try_send(i);
    }
    consumer.join();
    TEST_CHECK(received.load() == 6);
}

// --- MPSC 多生产者单消费者测试 ---

static void test_mpsc_basic() {
    kale::executor::MpscTaskChannel<int, 64> ch;
    TEST_CHECK(ch.empty());
    TEST_CHECK(ch.try_send(1));
    TEST_CHECK(ch.try_send(2));
    int v;
    TEST_CHECK(ch.try_recv(v));
    TEST_CHECK(v == 1);
    TEST_CHECK(ch.try_recv(v));
    TEST_CHECK(v == 2);
    TEST_CHECK(ch.empty());
}

static void test_mpsc_multi_producer() {
    kale::executor::MpscTaskChannel<int, 128> ch;
    constexpr int producers = 4;
    constexpr int per_producer = 500;
    std::atomic<int> total_received{0};

    std::vector<std::thread> prods;
    for (int p = 0; p < producers; ++p) {
        int id = p;
        prods.emplace_back([&, id] {
            for (int i = 0; i < per_producer; ++i) {
                int val = id * 10000 + i;
                while (!ch.try_send(val))
                    std::this_thread::yield();
            }
        });
    }

    std::thread consumer([&] {
        std::vector<int> received;
        received.reserve(producers * per_producer);
        while (received.size() < static_cast<size_t>(producers * per_producer)) {
            int v;
            if (ch.try_recv(v)) {
                received.push_back(v);
                total_received++;
            } else {
                std::this_thread::yield();
            }
        }
        std::sort(received.begin(), received.end());
        for (int p = 0; p < producers; ++p) {
            for (int i = 0; i < per_producer; ++i) {
                TEST_CHECK(received[p * per_producer + i] == p * 10000 + i);
            }
        }
    });

    for (auto& t : prods) t.join();
    consumer.join();
    TEST_CHECK(total_received.load() == producers * per_producer);
}

static void test_mpsc_full() {
    kale::executor::MpscTaskChannel<int, 8> ch;
    for (int i = 0; i < 8; ++i) TEST_CHECK(ch.try_send(i));
    TEST_CHECK(!ch.try_send(99));
    int v;
    TEST_CHECK(ch.try_recv(v));
    TEST_CHECK(v == 0);
    TEST_CHECK(ch.try_send(99));
}

static void test_mpsc_stress() {
    kale::executor::MpscTaskChannel<int, 256> ch;
    constexpr int producers = 8;
    constexpr int per_producer = 2000;

    std::vector<std::thread> prods;
    for (int p = 0; p < producers; ++p) {
        int id = p;
        prods.emplace_back([&, id] {
            for (int i = 0; i < per_producer; ++i) {
                while (!ch.try_send(id * 100000 + i))
                    std::this_thread::yield();
            }
        });
    }

    std::thread consumer([&] {
        std::vector<int> received;
        received.reserve(producers * per_producer);
        while (received.size() < static_cast<size_t>(producers * per_producer)) {
            int v;
            if (ch.try_recv(v))
                received.push_back(v);
            else
                std::this_thread::yield();
        }
        std::sort(received.begin(), received.end());
        for (int p = 0; p < producers; ++p) {
            for (int i = 0; i < per_producer; ++i) {
                TEST_CHECK(received[p * per_producer + i] == p * 100000 + i);
            }
        }
    });

    for (auto& t : prods) t.join();
    consumer.join();
}

static void test_make_mpsc_channel() {
    auto ch = kale::executor::make_mpsc_channel<int, 32>();
    TEST_CHECK(ch.capacity() >= 32);
    TEST_CHECK(ch.try_send(1));
    int v;
    TEST_CHECK(ch.try_recv(v));
    TEST_CHECK(v == 1);
}

int main() {
    std::cout << "TaskChannel tests..." << std::endl;
    test_basic_send_recv();
    std::cout << "  basic_send_recv OK" << std::endl;
    test_ordering();
    std::cout << "  ordering OK" << std::endl;
    test_full();
    std::cout << "  full OK" << std::endl;
    test_spsc_threaded();
    std::cout << "  spsc_threaded OK" << std::endl;
    test_blocking_recv();
    std::cout << "  blocking_recv OK" << std::endl;
    test_blocking_send();
    std::cout << "  blocking_send OK" << std::endl;
    test_timeout();
    std::cout << "  timeout OK" << std::endl;
    test_make_channel();
    std::cout << "  make_channel OK" << std::endl;
    test_stress();
    std::cout << "  stress OK" << std::endl;
    test_complex_type();
    std::cout << "  complex_type OK" << std::endl;
    test_complex_type_threaded();
    std::cout << "  complex_type_threaded OK" << std::endl;
    test_send_timeout();
    std::cout << "  send_timeout OK" << std::endl;
    test_send_timeout_zero();
    std::cout << "  send_timeout_zero OK" << std::endl;
    test_recv_timeout_zero();
    std::cout << "  recv_timeout_zero OK" << std::endl;
    test_capacity_small();
    std::cout << "  capacity_small OK" << std::endl;
    test_try_recv_preserves_out();
    std::cout << "  try_recv_preserves_out OK" << std::endl;
    test_producer_faster();
    std::cout << "  producer_faster OK" << std::endl;
    test_consumer_faster();
    std::cout << "  consumer_faster OK" << std::endl;
    test_blocking_multi_round();
    std::cout << "  blocking_multi_round OK" << std::endl;
    std::cout << "MpscTaskChannel tests..." << std::endl;
    test_mpsc_basic();
    std::cout << "  mpsc_basic OK" << std::endl;
    test_mpsc_multi_producer();
    std::cout << "  mpsc_multi_producer OK" << std::endl;
    test_mpsc_full();
    std::cout << "  mpsc_full OK" << std::endl;
    test_mpsc_stress();
    std::cout << "  mpsc_stress OK" << std::endl;
    test_make_mpsc_channel();
    std::cout << "  make_mpsc_channel OK" << std::endl;
    std::cout << "All tests passed." << std::endl;
    return 0;
}
