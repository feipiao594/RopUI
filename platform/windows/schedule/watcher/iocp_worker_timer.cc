#include "iocp_worker_timer.h"

#ifdef _WIN32

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <stdexcept>

#include "../iocp_backend.h"

namespace RopHive::Windows {

static std::atomic<ULONG_PTR> gTimerKey{0xC0DEC100};

class IocpTimerEventSource final : public IocpEventSource {
public:
    IocpTimerEventSource(ULONG_PTR key, std::function<void()> cb)
        : IocpEventSource(key), cb_(std::move(cb)) {}

    void dispatch(const void* raw_event) override {
        (void)raw_event;
        const uint64_t pending = pending_.exchange(0, std::memory_order_acq_rel);
        if (!cb_ || pending == 0) return;
        for (uint64_t i = 0; i < pending; ++i) {
            if (cb_) cb_();
        }
    }

    void notifyFromTimer(uint64_t count) {
        pending_.fetch_add(count, std::memory_order_acq_rel);
        auto* b = backend();
        if (!b) return;
        b->postWake(key());
    }

    void clearPending() {
        pending_.store(0, std::memory_order_release);
    }

private:
    std::function<void()> cb_;
    std::atomic<uint64_t> pending_{0};
};

struct IocpWorkerTimerState {
    HANDLE timer{nullptr};
    HANDLE wait_handle{nullptr};
    std::weak_ptr<IocpTimerEventSource> source;

    ~IocpWorkerTimerState() {
        if (wait_handle) {
            ::UnregisterWaitEx(wait_handle, INVALID_HANDLE_VALUE);
            wait_handle = nullptr;
        }
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

static VOID CALLBACK onWaitableTimerFire(PVOID context, BOOLEAN) {
    auto* state = static_cast<IocpWorkerTimerState*>(context);
    if (!state) return;
    auto src = state->source.lock();
    if (!src) return;
    src->notifyFromTimer(1);
}

IocpWorkerTimerWatcher::IocpWorkerTimerWatcher(IOWorker& worker, std::function<void()> callback)
    : IWorkerWatcher(worker) {
    state_ = std::make_shared<IocpWorkerTimerState>();

    HANDLE timer = ::CreateWaitableTimerExW(
        nullptr,
        nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!timer || timer == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("CreateWaitableTimerExW failed");
    }
    state_->timer = timer;

    auto src = std::make_shared<IocpTimerEventSource>(
        gTimerKey.fetch_add(1, std::memory_order_relaxed),
        std::move(callback));
    state_->source = src;
    source_ = std::move(src);

    if (!::RegisterWaitForSingleObject(
            &state_->wait_handle,
            state_->timer,
            onWaitableTimerFire,
            state_.get(),
            INFINITE,
            WT_EXECUTEDEFAULT)) {
        throw std::runtime_error("RegisterWaitForSingleObject failed");
    }
}

IocpWorkerTimerWatcher::~IocpWorkerTimerWatcher() {
    stop();
    source_.reset();
    state_.reset();
}

void IocpWorkerTimerWatcher::start() {
    if (attached_) return;
    attachSource(source_);
    attached_ = true;
}

void IocpWorkerTimerWatcher::stop() {
    if (!attached_) return;
    clearSpec();
    detachSource(source_);
    attached_ = false;
}

void IocpWorkerTimerWatcher::setSpec(std::chrono::nanoseconds initial_delay,
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

void IocpWorkerTimerWatcher::clearSpec() {
    if (!state_ || !state_->timer) return;
    ::CancelWaitableTimer(state_->timer);
    auto src = std::static_pointer_cast<IocpTimerEventSource>(source_);
    if (src) src->clearPending();
}

} // namespace RopHive::Windows

#endif // _WIN32
