#ifndef _ROP_PLATFORM_EVENTLOOP_H
#define _ROP_PLATFORM_EVENTLOOP_H

#include <cstdlib>
#include <sys/eventfd.h>
#include <unistd.h>

#include <deque>
#include <queue>
#include <vector>
#include <chrono>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>

#include "eventloop_core.h"
#include "../linux/schedule/epoll_backend.h"
#include "../linux/schedule/poll_backend.h"


namespace RopEventloop {

#define DEFAULT_BACKENDTYPE BackendType::LINUX_EPOLL

static std::unique_ptr<RopEventloop::IEventLoopCore>
createEventLoopCore(BackendType type) {
    using namespace RopEventloop::Linux;

    switch (type) {
    case BackendType::LINUX_POLL:
        return std::make_unique<PollEventLoopCore>();
    case BackendType::LINUX_EPOLL:
        return std::make_unique<EpollEventLoopCore>();
    default:
        std::abort();
        // Only support Linux poll/epoll now
    }
}

class IWatcher;
class IWakeUpWatcher;

class EventLoop {
public:
    using Task     = std::function<void()>;
    using Clock    = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::milliseconds;

    explicit EventLoop(BackendType backend_type = DEFAULT_BACKENDTYPE);
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void post(Task task);
    void postDelayed(Task task, Duration delay);

    void requestWakeUp();
    void requestExit();
    void run();

    bool exitRequested() const noexcept {
        return exit_requested_.load(std::memory_order_relaxed);
    }

protected:
    // Source management - Watcher-only.
    void attachSource(IEventSource* source);
    void detachSource(IEventSource* source);

private:
    friend class ::RopEventloop::IWatcher;

    struct TimerTask {
        TimePoint deadline;
        Task task;

        bool operator>(const TimerTask& rhs) const {
            return deadline > rhs.deadline;
        }
    };

    // Loop helpers
    void runExpiredTimers();
    void runReadyTasks();
    int  computeTimeoutMs();

private:
    BackendType backend_type_;
    std::unique_ptr<IEventLoopCore> core_;

    // wakeup watcher is backend-specific (epoll/poll/kqueue/iocp)
    std::unique_ptr<IWakeUpWatcher> wakeup_;

    // scheduling state
    std::mutex mu_;
    std::deque<Task> tasks_;

    std::priority_queue<
        TimerTask,
        std::vector<TimerTask>,
        std::greater<>
    > timers_;

    std::atomic<bool> exit_requested_{false};
};

class IWatcher {
public:
    virtual ~IWatcher() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

protected:
    explicit IWatcher(EventLoop& loop)
        : loop_(loop) {}

    void attachSource(IEventSource* src) {
        loop_.attachSource(src);
    }

    void detachSource(IEventSource* src) {
        loop_.detachSource(src);
    }

protected:
    EventLoop& loop_;
};

class IWakeUpWatcher : public IWatcher {
public:
    ~IWakeUpWatcher() override = default;
    virtual void notify() = 0;

protected:
    explicit IWakeUpWatcher(EventLoop& loop)
        : IWatcher(loop) {}
};

}

#endif // _ROP_PLATFORM_EVENTLOOP_H