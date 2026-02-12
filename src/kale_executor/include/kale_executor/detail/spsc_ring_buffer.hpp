// Kale 执行器层 - SPSC 无锁环形缓冲区
// 单生产者单消费者，基于 std::atomic 与 CAS

#pragma once

#include <kale_executor/detail/power_of_2.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace kale::executor::detail {

template <typename T, std::size_t Capacity>
class SpscRingBuffer {
    static constexpr std::size_t cap_ = round_up_to_power_of_2(Capacity) < 2
                                           ? 2
                                           : round_up_to_power_of_2(Capacity);

    std::array<std::optional<T>, cap_> slots_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};

public:
    static constexpr std::size_t capacity() { return cap_; }

    bool try_push(T&& value) {
        const std::size_t t = tail_.load(std::memory_order_seq_cst);
        const std::size_t h = head_.load(std::memory_order_seq_cst);
        if (t - h >= cap_) return false;
        slots_[t & (cap_ - 1)] = std::move(value);
        std::atomic_thread_fence(std::memory_order_release);
        tail_.store(t + 1, std::memory_order_seq_cst);
        return true;
    }

    bool try_pop(T& out) {
        const std::size_t h = head_.load(std::memory_order_seq_cst);
        const std::size_t t = tail_.load(std::memory_order_seq_cst);
        if (h == t) return false;
        std::atomic_thread_fence(std::memory_order_acquire);
        auto& slot = slots_[h & (cap_ - 1)];
        out = std::move(*slot);
        slot.reset();
        head_.store(h + 1, std::memory_order_seq_cst);
        return true;
    }

    std::size_t size() const {
        const std::size_t h = head_.load(std::memory_order_seq_cst);
        const std::size_t t = tail_.load(std::memory_order_seq_cst);
        return t - h;
    }

    bool empty() const { return size() == 0; }
};

}  // namespace kale::executor::detail
