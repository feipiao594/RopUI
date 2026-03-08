#include "win32_worker_timer.h"

#ifdef _WIN32

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>

#include "../win32_backend.h"

namespace RopHive::Windows {

struct Win32WorkerTimerState {
    HANDLE timer{nullptr};
    std::function<void()> cb{nullptr};

    ~Win32WorkerTimerState() {
        if (timer && timer != INVALID_HANDLE_VALUE) {
            ::CloseHandle(timer);
            timer = nullptr;
        }
    }
};

static LARGE_INTEGER toRelativeDueTime(std::chrono::nanoseconds delay) {
    const auto ns = delay > std::chrono::nanoseconds::zero() ? delay.count() : 1LL;
    long long ticks = ns / 100;
    if (ticks <= 0) ticks = 1;
    LARGE_INTEGER li;
    li.QuadPart = -ticks;
    return li;
}

static LONG toPeriodMs(std::chrono::nanoseconds interval) {
    if (interval <= std::chrono::nanoseconds::zero()) return 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(interval).count();
    const long long bounded = std::max<long long>(1, ms);
    constexpr long long kMax = static_cast<long long>(std::numeric_limits<LONG>::max());
    return static_cast<LONG>(bounded > kMax ? kMax : bounded);
}

Win32WorkerTimerWatcher::Win32WorkerTimerWatcher(IOWorker& worker, std::function<void()> callback)
    : IWorkerWatcher(worker) {
    state_ = std::make_shared<Win32WorkerTimerState>();
    state_->timer = ::CreateWaitableTimerExW(
        nullptr,
        nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!state_->timer || state_->timer == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("CreateWaitableTimerExW failed");
    }
    state_->cb = std::move(callback);

    auto state = state_;
    source_ = std::make_shared<Win32HandleEventSource>(
        state_->timer,
        [state](HANDLE) {
            if (state && state->cb) {
                state->cb();
            }
        });
}

Win32WorkerTimerWatcher::~Win32WorkerTimerWatcher() {
    stop();
    source_.reset();
    state_.reset();
}

void Win32WorkerTimerWatcher::start() {
    if (attached_) return;
    attachSource(source_);
    attached_ = true;
}

void Win32WorkerTimerWatcher::stop() {
    if (!attached_) return;
    clearSpec();
    detachSource(source_);
    attached_ = false;
}

void Win32WorkerTimerWatcher::setSpec(std::chrono::nanoseconds initial_delay,
                                      std::chrono::nanoseconds interval) {
    if (!state_ || !state_->timer) return;
    if (initial_delay <= std::chrono::nanoseconds::zero() &&
        interval <= std::chrono::nanoseconds::zero()) {
        clearSpec();
        return;
    }

    // when intervel is not zero, init is zero, use intervel
    const auto first =
        interval > std::chrono::nanoseconds::zero()
            ? (initial_delay > std::chrono::nanoseconds::zero() ? initial_delay : interval)
            : initial_delay;

    const LARGE_INTEGER due = toRelativeDueTime(first);
    const LONG period_ms = toPeriodMs(interval);

    ::SetWaitableTimer(state_->timer, &due, period_ms, nullptr, nullptr, FALSE);
}

void Win32WorkerTimerWatcher::clearSpec() {
    if (!state_ || !state_->timer) return;
    ::CancelWaitableTimer(state_->timer);
}

} // namespace RopHive::Windows

#endif // _WIN32
