#ifndef _ROP_PLATFORM_LINUX_EVENTLOOP_H
#define _ROP_PLATFORM_LINUX_EVENTLOOP_H

#include <cstdio>
#include <cstdlib>
#include <sys/eventfd.h>
#include <unistd.h>

#include <deque>
#include <queue>
#include <chrono>
#include <functional>
#include <memory>
#include <atomic>

#include "epoll_backend.h"
#include "poll_backend.h"


namespace RopEventloop::Linux {

enum class BackendType {
    Poll,
    Epoll,
};

static std::unique_ptr<IEventLoopCore>
createEventLoopCore(BackendType type) {
    using namespace RopEventloop::Linux;

    switch (type) {
    case BackendType::Poll:
        return std::make_unique<PollEventLoopCore>();
    case BackendType::Epoll:
        return std::make_unique<EpollEventLoopCore>();
    default:
        std::abort();
    }
}


class EventLoop {
public:
    using Task = std::function<void()>;
    using Clock = std::chrono::steady_clock;

    explicit EventLoop(BackendType backend)
        : core_(createEventLoopCore(backend)) {
        initWakeup();
    }

    ~EventLoop() {
        if (wakeup_fd_ >= 0) {
            ::close(wakeup_fd_);
        }
    }

    /* ---------- public API ---------- */

    void post(Task task) {
        tasks_.push_back(std::move(task));
        wakeup();
    }

    void postDelayed(Task task, std::chrono::milliseconds delay) {
        timers_.push(TimerTask{
            Clock::now() + delay,
            std::move(task)
        });
        wakeup();
    }

    void requestExit() {
        exit_requested_.store(true, std::memory_order_relaxed);
        wakeup();
    }

    void addSource(std::unique_ptr<IEventSource> src) {
        core_->addSource(std::move(src));
    }

    void run() {
        exit_requested_.store(false, std::memory_order_relaxed);
        core_->applyInitialChanges();

        while (!exit_requested_.load(std::memory_order_relaxed)) {
            runReadyTasks();
            int timeout_ms = computeTimeoutMs();
            core_->runOnce(timeout_ms);
        }
    }

private:
    /* ---------- timer ---------- */

    struct TimerTask {
        Clock::time_point deadline;
        Task task;

        bool operator>(const TimerTask& rhs) const {
            return deadline > rhs.deadline;
        }
    };

    /* ---------- wakeup ---------- */

    void initWakeup() {
        wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wakeup_fd_ < 0) {
            perror("eventfd");
            std::abort();
        }

        // wakeup fd 只关心可读
        auto wakeup_cb = [this](short /*revents*/) {
            uint64_t val;
            while (::read(wakeup_fd_, &val, sizeof(val)) > 0) {
                // drain
            }
        };

        using namespace RopEventloop::Linux;

        std::unique_ptr<IEventSource> src;

        if (dynamic_cast<PollEventLoopCore*>(core_.get())) {
            src = std::make_unique<PollReadinessEventSource>(
                wakeup_fd_, POLLIN, wakeup_cb);
        } else {
            src = std::make_unique<EpollReadinessEventSource>(
                wakeup_fd_, POLLIN, wakeup_cb);
        }

        core_->addSource(std::move(src));
    }

    void wakeup() {
        uint64_t one = 1;
        ::write(wakeup_fd_, &one, sizeof(one));
    }

    /* ---------- scheduling ---------- */

    void runReadyTasks() {
        // 普通任务
        while (!tasks_.empty()) {
            Task task = std::move(tasks_.front());
            tasks_.pop_front();
            task();
        }

        // 到期 timer
        auto now = Clock::now();
        while (!timers_.empty() &&
               timers_.top().deadline <= now) {
            Task task = std::move(timers_.top().task);
            timers_.pop();
            task();
        }
    }

    int computeTimeoutMs() const {
        if (!tasks_.empty()) {
            return 0;
        }

        if (timers_.empty()) {
            return -1;
        }

        auto now = Clock::now();
        auto deadline = timers_.top().deadline;

        if (deadline <= now) {
            return 0;
        }

        auto diff =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now)
                .count();

        return static_cast<int>(diff);
    }

private:
    std::unique_ptr<IEventLoopCore> core_;

    std::deque<Task> tasks_;
    std::priority_queue<
        TimerTask,
        std::vector<TimerTask>,
        std::greater<>
    > timers_;

    int wakeup_fd_ = -1;
    std::atomic<bool> exit_requested_{false};
};

}
#endif // _ROP_PLATFORM_LINUX_EVENTLOOP_H