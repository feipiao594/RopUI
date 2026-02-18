#pragma once

#include <coroutine>
#include <deque>
#include <exception>
#include <mutex>
#include <memory>
#include <optional>
#include <type_traits>
#include <string_view>
#include <functional>
#include <utility>
#include <chrono>

// Executor is built on top of IOWorker.
#include <platform/schedule/io_worker.h>
#include <platform/network/ip_endpoint.h>

namespace asyncnet {

struct Executor {
    explicit Executor(::RopHive::IOWorker& worker) : worker(worker) {}
    void schedule(std::coroutine_handle<> h);
    ::RopHive::IOWorker& worker;
};

// A minimal single-producer/multi-producer -> single-consumer async queue.
// - `pop()` is awaitable and resumes on `Executor::schedule()`.
// - Must call `bind(exec)` before any awaiting/wakeup can work.
template <class T>
class AsyncQueue {
public:
    void push(T v) {
        std::optional<std::coroutine_handle<>> waiter;
        Executor* exec = nullptr;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (closed_) return;
            q_.push_back(std::move(v));
            exec = exec_;
            waiter = waiter_;
            waiter_.reset();
        }
        wakeOne(exec, waiter);
    }

    void close() {
        std::optional<std::coroutine_handle<>> waiter;
        Executor* exec = nullptr;
        {
            std::lock_guard<std::mutex> lk(mu_);
            closed_ = true;
            exec = exec_;
            waiter = waiter_;
            waiter_.reset();
        }
        wakeOne(exec, waiter);
    }

    struct PopAwaitable {
        AsyncQueue& q;
        bool await_ready() const noexcept {
            std::lock_guard<std::mutex> lk(q.mu_);
            return q.closed_ || !q.q_.empty();
        }
        void await_suspend(std::coroutine_handle<> h) {
            std::lock_guard<std::mutex> lk(q.mu_);
            q.waiter_ = h;
        }
        std::optional<T> await_resume() {
            std::lock_guard<std::mutex> lk(q.mu_);
            if (q.q_.empty()) return std::nullopt;
            T out = std::move(q.q_.front());
            q.q_.pop_front();
            return out;
        }
    };

    PopAwaitable pop() { return PopAwaitable{*this}; }

    void bind(Executor& exec) {
        std::optional<std::coroutine_handle<>> waiter;
        bool should_wake = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            exec_ = &exec;
            should_wake = waiter_.has_value() && (closed_ || !q_.empty());
            if (should_wake) {
                waiter = waiter_;
                waiter_.reset();
            }
        }
        if (should_wake) {
            wakeOne(&exec, waiter);
        }
    }

private:
    static void wakeOne(Executor* exec, const std::optional<std::coroutine_handle<>>& waiter) {
        if (!exec || !waiter) return;
        exec->schedule(*waiter);
    }

private:
    mutable std::mutex mu_;
    Executor* exec_{nullptr};
    bool closed_{false};
    std::deque<T> q_;
    std::optional<std::coroutine_handle<>> waiter_;
};

// Single-thread assumption: the currently running executor (set while resuming tasks).
inline thread_local Executor* tls_current_executor = nullptr;

template <class T>
class Task;

template <>
class Task<void>;

// Fire-and-forget task start helper.
void spawn(Executor& exec, Task<void> task);

class TaskGroup;
TaskGroup taskGroup(Executor& exec);

// Minimal TCP APIs.
class AsyncTcpListener;
class AsyncTcpStream;

// Listen on a specific local bind endpoint (no DNS).
std::shared_ptr<AsyncTcpListener> listen(Executor& exec, ::RopHive::Network::IpEndpoint local_bind);
// Convenience: listen on 0.0.0.0:<port>.
std::shared_ptr<AsyncTcpListener> listen(Executor& exec, int port);

Task<std::shared_ptr<AsyncTcpStream>> accept(std::shared_ptr<AsyncTcpListener> listener);
Task<std::optional<std::string>> recvSome(std::shared_ptr<AsyncTcpStream> stream);
Task<void> sendAll(std::shared_ptr<AsyncTcpStream> stream, std::string data);
Task<std::shared_ptr<AsyncTcpStream>> connect(Executor& exec, std::string host, int port);
Task<void> sleepFor(Executor& exec, std::chrono::milliseconds delay);

// Multi-worker server helper:
// - Runs accept loop on `accept_exec` (typically worker 0).
// - Round-robins accepted fds to target workers via Hive::postToWorker.
// - Creates TcpStream on the target worker's Executor (based on new tcp watchers),
//   then spawns the handler.
//
// The handler is not exposed to worker ids/fds. If needed, the handler can query
// `exec.worker.id()` for the current worker id.
using ConnectionHandler = std::function<Task<void>(Executor& exec, std::shared_ptr<AsyncTcpStream> stream)>;

Task<void> serveRoundRobin(Executor& accept_exec,
                           ::RopHive::Hive& hive,
                           int worker_n,
                           std::shared_ptr<std::vector<std::shared_ptr<Executor>>> execs,
                           ::RopHive::Network::IpEndpoint local_bind,
                           ConnectionHandler handler);

class TaskGroup {
public:
    explicit TaskGroup(Executor& exec);

    // Starts a child task and keeps it in the group.
    void spawn(Task<void> task);

    // Waits until all currently spawned tasks complete.
    Task<void> join();

private:
    struct State;
    std::shared_ptr<State> state_;

    static Task<void> childWrapper(std::shared_ptr<State> st, Task<void> task);
};

// ---- Task implementation (header-only) ----
template <class T>
class Task {
public:
    struct promise_type {
        Task get_return_object() noexcept {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto& p = h.promise();
                if (p.detached) {
                    h.destroy();
                    return;
                }
                if (p.continuation) {
                    p.exec->schedule(*p.continuation);
                }
            }
            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }
        void unhandled_exception() { exception = std::current_exception(); }

        template <class U>
        void return_value(U&& v) noexcept(std::is_nothrow_constructible_v<T, U&&>) {
            value.emplace(std::forward<U>(v));
        }

        Executor* exec{nullptr};
        std::optional<std::coroutine_handle<>> continuation;
        std::optional<T> value;
        std::exception_ptr exception;
        bool detached{false};
    };

    Task() = default;
    explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}
    Task(Task&& other) noexcept : h_(std::exchange(other.h_, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (h_) h_.destroy();
            h_ = std::exchange(other.h_, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() {
        if (h_) h_.destroy();
    }

    void start(Executor& exec) {
        h_.promise().exec = &exec;
        exec.schedule(h_);
    }

    void detach(Executor& exec) {
        h_.promise().exec = &exec;
        h_.promise().detached = true;
        exec.schedule(h_);
        h_ = {};
    }

    struct Awaiter {
        std::coroutine_handle<promise_type> h;
        bool await_ready() noexcept { return !h || h.done(); }
        void await_suspend(std::coroutine_handle<> cont) noexcept {
            h.promise().continuation = cont;
            if (!h.promise().exec) {
                h.promise().exec = tls_current_executor;
            }
            if (!h.promise().exec) {
                // Programming error: awaiting a Task outside of an executor resume context.
                const std::string_view msg =
                    "asyncnet fatal: Task awaited without bound Executor (tls_current_executor is null)\n";
                LOG(INFO)("%s",msg.data());
                std::abort();
            }
            h.promise().exec->schedule(h);
        }
        T await_resume() {
            auto& p = h.promise();
            if (p.exception) std::rethrow_exception(p.exception);
            return std::move(*p.value);
        }
    };

    auto operator co_await() noexcept { return Awaiter{h_}; }

private:
    std::coroutine_handle<promise_type> h_{};
};

template <>
class Task<void> {
public:
    struct promise_type {
        Task get_return_object() noexcept {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto& p = h.promise();
                if (p.detached) {
                    h.destroy();
                    return;
                }
                if (p.continuation) {
                    p.exec->schedule(*p.continuation);
                }
            }
            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { exception = std::current_exception(); }

        Executor* exec{nullptr};
        std::optional<std::coroutine_handle<>> continuation;
        std::exception_ptr exception;
        bool detached{false};
    };

    Task() = default;
    explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}
    Task(Task&& other) noexcept : h_(std::exchange(other.h_, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (h_) h_.destroy();
            h_ = std::exchange(other.h_, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() {
        if (h_) h_.destroy();
    }

    void start(Executor& exec) {
        h_.promise().exec = &exec;
        exec.schedule(h_);
    }

    void detach(Executor& exec) {
        h_.promise().exec = &exec;
        h_.promise().detached = true;
        exec.schedule(h_);
        h_ = {};
    }

    struct Awaiter {
        std::coroutine_handle<promise_type> h;
        bool await_ready() noexcept { return !h || h.done(); }
        void await_suspend(std::coroutine_handle<> cont) noexcept {
            h.promise().continuation = cont;
            if (!h.promise().exec) {
                h.promise().exec = tls_current_executor;
            }
            if (!h.promise().exec) {
                const std::string_view msg =
                    "asyncnet fatal: Task<void> awaited without bound Executor (tls_current_executor is null)\n";
                LOG(INFO)("%s",msg.data());
                std::abort();
            }
            h.promise().exec->schedule(h);
        }
        void await_resume() {
            auto& p = h.promise();
            if (p.exception) std::rethrow_exception(p.exception);
        }
    };

    auto operator co_await() noexcept { return Awaiter{h_}; }

private:
    std::coroutine_handle<promise_type> h_{};
};

} // namespace asyncnet
