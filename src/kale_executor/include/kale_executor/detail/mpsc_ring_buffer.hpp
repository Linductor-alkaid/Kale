// Kale 执行器层 - MPSC 无锁环形缓冲区
// 多生产者单消费者，基于 Dmitry Vyukov Bounded MPMC 算法

#pragma once

#include <kale_executor/detail/power_of_2.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>

namespace kale::executor::detail {

template <typename T, std::size_t Capacity>
class MpscRingBuffer {
    static constexpr std::size_t cap_ = round_up_to_power_of_2(Capacity) < 2
                                            ? 2
                                            : round_up_to_power_of_2(Capacity);
    static constexpr std::size_t mask_ = cap_ - 1;

    struct Cell {
        std::atomic<std::size_t> sequence;
        std::optional<T> data;
    };

    std::array<Cell, cap_> cells_;
    std::atomic<std::size_t> enqueue_pos_{0};
    std::atomic<std::size_t> dequeue_pos_{0};

    void init_sequences() {
        for (std::size_t i = 0; i < cap_; ++i) {
            cells_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

public:
    MpscRingBuffer() { init_sequences(); }

    static constexpr std::size_t capacity() { return cap_; }

    bool try_push(T&& value) {
        std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        Cell* cell;
        for (;;) {
            cell = &cells_[pos & mask_];
            std::size_t seq =
                cell->sequence.load(std::memory_order_acquire);
            std::intptr_t dif = static_cast<std::intptr_t>(seq) -
                               static_cast<std::intptr_t>(pos);
            if (dif == 0) {
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed))
                    break;
            } else if (dif < 0) {
                return false;
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
        cell->data = std::move(value);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool try_push(const T& value) {
        T tmp = value;
        return try_push(std::move(tmp));
    }

    bool try_pop(T& out) {
        std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        Cell* cell;
        for (;;) {
            cell = &cells_[pos & mask_];
            std::size_t seq =
                cell->sequence.load(std::memory_order_acquire);
            std::intptr_t dif = static_cast<std::intptr_t>(seq) -
                               static_cast<std::intptr_t>(pos + 1);
            if (dif == 0) {
                if (dequeue_pos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed))
                    break;
            } else if (dif < 0) {
                return false;
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
        out = std::move(*cell->data);
        cell->data.reset();
        cell->sequence.store(pos + mask_ + 1, std::memory_order_release);
        return true;
    }

    std::size_t size() const {
        std::size_t enq = enqueue_pos_.load(std::memory_order_seq_cst);
        std::size_t deq = dequeue_pos_.load(std::memory_order_seq_cst);
        return enq - deq;
    }

    bool empty() const { return size() == 0; }
};

}  // namespace kale::executor::detail
