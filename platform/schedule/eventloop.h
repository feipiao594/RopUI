#ifndef _ROP_PLATFORM_EVENTLOOP_H
#define _ROP_PLATFORM_EVENTLOOP_H

#include <cstdlib>
#include <deque>
#include <queue>
#include <vector>
#include <chrono>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>

#ifdef __linux__
#include <sys/eventfd.h>
#include <unistd.h>
#endif

#include "eventloop_core.h"
#include "log.hpp"

#ifdef __linux__
#include "../linux/schedule/epoll_backend.h"
#include "../linux/schedule/poll_backend.h"
#define DEFAULT_BACKENDTYPE BackendType::LINUX_EPOLL
#endif

#ifdef __APPLE__
    #include "../macos/schedule/poll_backend.h"
    #include "../macos/schedule/kqueue_backend.h"
    #include "../macos/schedule/cocoa_backend.h"
    #define DEFAULT_BACKENDTYPE BackendType::MACOS_KQUEUE
#endif

#ifdef _WIN32
    #include "../windows/schedule/win32_backend.h"
    #include "../windows/schedule/iocp_backend.h"
    #define DEFAULT_BACKENDTYPE BackendType::WINDOWS_IOCP
#endif

namespace RopHive {

static std::unique_ptr<RopHive::IEventLoopCore>
createEventLoopCore(BackendType type) {
    using namespace RopHive;

    switch (type) {
    #ifdef __linux__
    case BackendType::LINUX_POLL:
        return std::make_unique<Linux::PollEventLoopCore>();
    case BackendType::LINUX_EPOLL:
        return std::make_unique<Linux::EpollEventLoopCore>();
    #endif
    #ifdef __APPLE__
    case BackendType::MACOS_POLL:
        return std::make_unique<MacOS::PollEventLoopCore>();
    case BackendType::MACOS_KQUEUE:
        return std::make_unique<MacOS::KqueueEventLoopCore>();
    case BackendType::MACOS_COCOA:
        return std::make_unique<MacOS::CocoaEventLoopCore>();
    #endif
    #ifdef _WIN32
    case BackendType::WINDOWS_WIN32:
        return std::make_unique<Windows::Win32EventLoopCore>();
    case BackendType::WINDOWS_IOCP:
        return std::make_unique<Windows::IocpEventLoopCore>();
    #endif
    default:
        LOG(FATAL)("Unsupported backend type, id: %d, maybe it not match your system", type);
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
    friend class ::RopHive::IWatcher;

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
