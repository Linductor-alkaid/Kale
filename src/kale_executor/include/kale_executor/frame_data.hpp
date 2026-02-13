// Kale 执行器层 - FrameData 与 SwapBuffer
// 帧作用域双缓冲/三缓冲，供渲染管线等使用

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>

namespace kale::executor {

/// 双缓冲/三缓冲：写者写入当前槽，读者读取上一帧已提交槽；swap() 由单一协调者调用
template <typename T, size_t N = 2>
class SwapBuffer {
public:
    static_assert(N >= 2 && N <= 8, "SwapBuffer N must be in [2, 8]");

    SwapBuffer() : write_index_(0), read_index_(N - 1) {}

    /// 当前写入缓冲区（写者独占）
    T& current_for_writer() { return buffers_[write_index_.load(std::memory_order_relaxed)]; }
    const T& current_for_writer() const {
        return buffers_[write_index_.load(std::memory_order_relaxed)];
    }

    /// 当前只读快照（上一帧或已提交）
    const T& current_for_reader() const {
        return buffers_[read_index_.load(std::memory_order_acquire)];
    }

    /// 帧末由单一协调者调用，交换读写槽
    void swap() {
        std::lock_guard<std::mutex> lock(swap_mutex_);
        size_t w = write_index_.load(std::memory_order_relaxed);
        read_index_.store(w, std::memory_order_release);
        write_index_.store((w + 1) % N, std::memory_order_relaxed);
    }

private:
    std::array<T, N> buffers_{};
    std::atomic<size_t> write_index_;
    std::atomic<size_t> read_index_;
    mutable std::mutex swap_mutex_;
};

/// 帧作用域数据：write_buffer / read_buffer / end_frame，内部双缓冲
template <typename T>
class FrameData {
public:
    FrameData() = default;

    /// 获取当前写入缓冲区
    T& write_buffer() { return swap_buffer_.current_for_writer(); }
    const T& write_buffer() const { return swap_buffer_.current_for_writer(); }

    /// 获取当前只读快照（上一帧或已提交）
    const T& read_buffer() const { return swap_buffer_.current_for_reader(); }

    /// 帧末交换缓冲区
    void end_frame() { swap_buffer_.swap(); }

private:
    SwapBuffer<T, 2> swap_buffer_;
};

/// 三缓冲帧数据（可选）
template <typename T>
class FrameDataTriple {
public:
    T& write_buffer() { return swap_buffer_.current_for_writer(); }
    const T& write_buffer() const { return swap_buffer_.current_for_writer(); }
    const T& read_buffer() const { return swap_buffer_.current_for_reader(); }
    void end_frame() { swap_buffer_.swap(); }

private:
    SwapBuffer<T, 3> swap_buffer_;
};

} // namespace kale::executor
