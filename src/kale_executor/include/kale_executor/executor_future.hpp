// Kale 执行器层 - ExecutorPromise / ExecutorFuture
// 基于 std::promise/future，支持 then 续接与 async_load API

#pragma once

#include <executor/executor.hpp>

#include <exception>
#include <future>
#include <type_traits>
#include <utility>

namespace kale::executor {

template <typename T>
class ExecutorFuture;

template <typename T>
class ExecutorPromise {
    std::promise<T> promise_;

public:
    ExecutorPromise() = default;

    ExecutorPromise(ExecutorPromise&&) noexcept = default;
    ExecutorPromise& operator=(ExecutorPromise&&) noexcept = default;
    ExecutorPromise(const ExecutorPromise&) = delete;
    ExecutorPromise& operator=(const ExecutorPromise&) = delete;

    void set_value(const T& value) { promise_.set_value(value); }
    void set_value(T&& value) { promise_.set_value(std::move(value)); }
    void set_exception(std::exception_ptr e) { promise_.set_exception(std::move(e)); }

    ExecutorFuture<T> get_future();
};

template <>
class ExecutorPromise<void> {
    std::promise<void> promise_;

public:
    ExecutorPromise() = default;

    ExecutorPromise(ExecutorPromise&&) noexcept = default;
    ExecutorPromise& operator=(ExecutorPromise&&) noexcept = default;
    ExecutorPromise(const ExecutorPromise&) = delete;
    ExecutorPromise& operator=(const ExecutorPromise&) = delete;

    void set_value() { promise_.set_value(); }
    void set_exception(std::exception_ptr e) { promise_.set_exception(std::move(e)); }

    ExecutorFuture<void> get_future();
};

template <typename T>
class ExecutorFuture {
    std::future<T> future_;

public:
    ExecutorFuture() = default;

    explicit ExecutorFuture(std::future<T> f) : future_(std::move(f)) {}

    ExecutorFuture(ExecutorFuture&&) noexcept = default;
    ExecutorFuture& operator=(ExecutorFuture&&) noexcept = default;
    ExecutorFuture(const ExecutorFuture&) = delete;
    ExecutorFuture& operator=(const ExecutorFuture&) = delete;

    T get() { return future_.get(); }

    bool valid() const { return future_.valid(); }

    template <typename Executor, typename F>
    ExecutorFuture<std::invoke_result_t<F, T>> then(Executor& ex, F&& func);
};

template <>
class ExecutorFuture<void> {
    std::future<void> future_;

public:
    ExecutorFuture() = default;

    explicit ExecutorFuture(std::future<void> f) : future_(std::move(f)) {}

    ExecutorFuture(ExecutorFuture&&) noexcept = default;
    ExecutorFuture& operator=(ExecutorFuture&&) noexcept = default;
    ExecutorFuture(const ExecutorFuture&) = delete;
    ExecutorFuture& operator=(const ExecutorFuture&) = delete;

    void get() { future_.get(); }

    bool valid() const { return future_.valid(); }

    template <typename Executor, typename F>
    ExecutorFuture<std::invoke_result_t<F>> then(Executor& ex, F&& func);
};

template <typename T>
ExecutorFuture<T> ExecutorPromise<T>::get_future() {
    return ExecutorFuture<T>(promise_.get_future());
}

ExecutorFuture<void> ExecutorPromise<void>::get_future() {
    return ExecutorFuture<void>(promise_.get_future());
}

template <typename T>
template <typename Executor, typename F>
ExecutorFuture<std::invoke_result_t<F, T>> ExecutorFuture<T>::then(Executor& ex, F&& func) {
    using R = std::invoke_result_t<F, T>;
    ExecutorPromise<R> next_promise;
    auto next_future = next_promise.get_future();

    std::future<T> captured = std::move(future_);
    ex.submit([captured = std::move(captured), func = std::forward<F>(func),
               promise = std::move(next_promise)]() mutable {
        try {
            T val = captured.get();
            if constexpr (std::is_void_v<R>) {
                std::invoke(func, std::move(val));
                promise.set_value();
            } else {
                promise.set_value(std::invoke(func, std::move(val)));
            }
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
    });

    return next_future;
}

template <typename Executor, typename F>
ExecutorFuture<std::invoke_result_t<F>> ExecutorFuture<void>::then(Executor& ex, F&& func) {
    using R = std::invoke_result_t<F>;
    ExecutorPromise<R> next_promise;
    auto next_future = next_promise.get_future();

    std::future<void> captured = std::move(future_);
    ex.submit([captured = std::move(captured), func = std::forward<F>(func),
               promise = std::move(next_promise)]() mutable {
        try {
            captured.get();
            if constexpr (std::is_void_v<R>) {
                std::invoke(func);
                promise.set_value();
            } else {
                promise.set_value(std::invoke(func));
            }
        } catch (...) {
            promise.set_exception(std::current_exception());
        }
    });

    return next_future;
}

// async_load API
template <typename T, typename F>
ExecutorFuture<T> async_load(::executor::Executor& ex, F&& loader) {
    auto fut = ex.submit(std::forward<F>(loader));
    return ExecutorFuture<T>(std::move(fut));
}

}  // namespace kale::executor
