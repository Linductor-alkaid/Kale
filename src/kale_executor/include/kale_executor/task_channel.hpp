// Kale 执行器层 - TaskChannel 无锁消息通道
// SPSC 单生产者单消费者 / MPSC 多生产者单消费者

#pragma once

#include <kale_executor/detail/mpsc_ring_buffer.hpp>
#include <kale_executor/detail/spsc_ring_buffer.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace kale::executor {

template <typename T, std::size_t Capacity = 64>
class TaskChannel {
    detail::SpscRingBuffer<T, Capacity> buffer_;
    mutable std::mutex mutex_;
    std::condition_variable cv_not_full_;
    std::condition_variable cv_not_empty_;

public:
    bool try_send(T&& value) {
        if (!buffer_.try_push(std::move(value))) return false;
        cv_not_empty_.notify_one();
        return true;
    }

    bool try_send(const T& value) {
        T tmp = value;
        return try_send(std::move(tmp));
    }

    bool try_recv(T& out) {
        if (!buffer_.try_pop(out)) return false;
        cv_not_full_.notify_one();
        return true;
    }

    bool send(T&& value, std::chrono::milliseconds timeout = {}) {
        if (timeout.count() == 0) return try_send(std::move(value));
        std::optional<T> opt(std::move(value));
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (opt) {
            if (buffer_.try_push(std::move(*opt))) {
                opt.reset();
                cv_not_empty_.notify_one();
                return true;
            }
            std::unique_lock lock(mutex_);
            if (opt && buffer_.try_push(std::move(*opt))) {
                opt.reset();
                cv_not_empty_.notify_one();
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) return false;
            cv_not_full_.wait_until(lock, deadline);
        }
        return false;
    }

    bool recv(T& out, std::chrono::milliseconds timeout = {}) {
        if (timeout.count() == 0) return try_recv(out);
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            if (buffer_.try_pop(out)) {
                cv_not_full_.notify_one();
                return true;
            }
            std::unique_lock lock(mutex_);
            if (buffer_.try_pop(out)) {
                cv_not_full_.notify_one();
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) return false;
            cv_not_empty_.wait_until(lock, deadline);
        }
    }

    std::size_t size() const { return buffer_.size(); }

    bool empty() const { return buffer_.empty(); }

    static constexpr std::size_t capacity() {
        return detail::SpscRingBuffer<T, Capacity>::capacity();
    }
};

template <typename T, std::size_t Cap = 64>
TaskChannel<T, Cap> make_channel() {
    return TaskChannel<T, Cap>{};
}

// MPSC 多生产者单消费者通道
template <typename T, std::size_t Capacity = 64>
class MpscTaskChannel {
    detail::MpscRingBuffer<T, Capacity> buffer_;
    mutable std::mutex mutex_;
    std::condition_variable cv_not_full_;
    std::condition_variable cv_not_empty_;

public:
    bool try_send(T&& value) {
        if (!buffer_.try_push(std::move(value))) return false;
        cv_not_empty_.notify_one();
        return true;
    }

    bool try_send(const T& value) {
        T tmp = value;
        return try_send(std::move(tmp));
    }

    bool try_recv(T& out) {
        if (!buffer_.try_pop(out)) return false;
        cv_not_full_.notify_one();
        return true;
    }

    bool send(T&& value, std::chrono::milliseconds timeout = {}) {
        if (timeout.count() == 0) return try_send(std::move(value));
        std::optional<T> opt(std::move(value));
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (opt) {
            if (buffer_.try_push(std::move(*opt))) {
                opt.reset();
                cv_not_empty_.notify_one();
                return true;
            }
            std::unique_lock lock(mutex_);
            if (opt && buffer_.try_push(std::move(*opt))) {
                opt.reset();
                cv_not_empty_.notify_one();
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) return false;
            cv_not_full_.wait_until(lock, deadline);
        }
        return false;
    }

    bool recv(T& out, std::chrono::milliseconds timeout = {}) {
        if (timeout.count() == 0) return try_recv(out);
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            if (buffer_.try_pop(out)) {
                cv_not_full_.notify_one();
                return true;
            }
            std::unique_lock lock(mutex_);
            if (buffer_.try_pop(out)) {
                cv_not_full_.notify_one();
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline) return false;
            cv_not_empty_.wait_until(lock, deadline);
        }
    }

    std::size_t size() const { return buffer_.size(); }

    bool empty() const { return buffer_.empty(); }

    static constexpr std::size_t capacity() {
        return detail::MpscRingBuffer<T, Capacity>::capacity();
    }
};

template <typename T, std::size_t Cap = 64>
MpscTaskChannel<T, Cap> make_mpsc_channel() {
    return MpscTaskChannel<T, Cap>{};
}

}  // namespace kale::executor
