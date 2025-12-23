#include <memory>
#include <stdexcept>
#include <utility>

#include "linux/schedule/watcher/epoll_wakeup.h"
#include "linux/schedule/watcher/poll_wakeup.h"
#include "schedule/eventloop_core.h"


namespace RopEventloop {

EventLoop::EventLoop(BackendType backend_type)
    : backend_type_(backend_type),
      core_(createEventLoopCore(backend_type)) {

    if (!core_) {
        throw std::runtime_error("Linux::EventLoop: createEventLoopCore returned null");
    }

    // Create wakeup watcher per backend type.
    switch (backend_type_) {
    case BackendType::LINUX_EPOLL:
        wakeup_ = std::make_unique<Linux::EpollWakeUpWatcher>(*this);
        break;
    case BackendType::LINUX_POLL:
        wakeup_ = std::make_unique<Linux::PollWakeUpWatcher>(*this);
        break;
    default:
        throw std::runtime_error("Linux::EventLoop: unknown backend");
    }

    // Ensure all initial sources (including wakeup source) are registered before running.
    core_->applyInitialChanges();
}

EventLoop::~EventLoop() {
    exit_requested_.store(true, std::memory_order_relaxed);
    wakeup_.reset();
    // core_ owns remaining sources; external watchers must be destroyed before EventLoop.
}

void EventLoop::post(Task task) {
    if (!task) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        tasks_.push_back(std::move(task));
    }
    wakeup_->notify();
}

void EventLoop::postDelayed(Task task, Duration delay) {
    if (!task) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        timers_.push(TimerTask{Clock::now() + delay, std::move(task)});
    }
    wakeup_->notify();
}

void EventLoop::requestExit() {
    exit_requested_.store(true, std::memory_order_relaxed);
    if (wakeup_) wakeup_->notify();
}

void EventLoop::attachSource(IEventSource* source) {
    if (!source) return;
    core_->addSource(source);
}

void EventLoop::detachSource(IEventSource* source) {
    if (!source) return;
    core_->removeSource(source);
}

void EventLoop::run() {
    while (!exitRequested()) {
        int timeout_ms = computeTimeoutMs();
        core_->runOnce(timeout_ms);

        runExpiredTimers();
        runReadyTasks();
    }
}

void EventLoop::runReadyTasks() {
    std::deque<Task> local;
    {
        std::lock_guard<std::mutex> lock(mu_);
        local.swap(tasks_);
    }

    while (!local.empty()) {
        auto task = std::move(local.front());
        local.pop_front();
        task();
    }
}

void EventLoop::runExpiredTimers() {
    std::vector<Task> expired;
    const auto now = Clock::now();

    {
        std::lock_guard<std::mutex> lock(mu_);
        while (!timers_.empty() && timers_.top().deadline <= now) {
            expired.push_back(std::move(timers_.top().task));
            timers_.pop();
        }
    }

    for (auto& t : expired) {
        t();
    }
}

int EventLoop::computeTimeoutMs() {
    if (exitRequested()) return 0;

    std::lock_guard<std::mutex> lock(mu_);

    if (!tasks_.empty()) return 0;
    if (timers_.empty()) return -1;

    const auto now = Clock::now();
    const auto deadline = timers_.top().deadline;

    if (deadline <= now) return 0;

    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    auto ms = delta.count();
    if (ms > static_cast<decltype(ms)>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(ms);
}

void EventLoop::requestWakeUp() {
    wakeup_->notify();
}
}