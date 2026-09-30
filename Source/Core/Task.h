#pragma once
#include <coroutine>
#include <iostream>
#include <optional>

template <typename T>
class Task {
public:
    struct promise_type {
        std::optional<T> result;

        Task<T> get_return_object() {
            return Task{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        void return_value(T value) {
            result = value;
        }

        void unhandled_exception() {
            std::terminate();  // 예외 처리
        }
    };

    Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~Task() { if (handle) handle.destroy(); }

    T get() {
        handle.resume();
        return handle.promise().result.value();
    }

private:
    std::coroutine_handle<promise_type> handle;
};
