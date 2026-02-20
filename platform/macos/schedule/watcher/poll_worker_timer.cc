#include "poll_worker_timer.h"

#ifdef __APPLE__

#include <sys/event.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "../poll_backend.h"

namespace RopHive::MacOS {

struct PollWorkerTimerState {
    int kq_fd{-1};
    uintptr_t ident{1};
    std::function<void()> cb{nullptr};
    bool promote_to_periodic{false};
    intptr_t periodic_ms{0};

    ~PollWorkerTimerState() {
        if (kq_fd >= 0) {
            ::close(kq_fd);
            kq_fd = -1;
        }
    }
};

static intptr_t toTimerMillis(std::chrono::nanoseconds ns) {
    if (ns <= std::chrono::nanoseconds::zero()) return 1;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(ns);
    const auto count = ms.count() > 0 ? ms.count() : 1;
    constexpr auto kMax = static_cast<long long>(std::numeric_limits<intptr_t>::max());
    return static_cast<intptr_t>(count > kMax ? kMax : count);
}

static void armTimerKqueue(const std::shared_ptr<PollWorkerTimerState>& state,
                           intptr_t interval_ms,
                           bool oneshot) {
    if (!state || state->kq_fd < 0) return;
    struct kevent kev;
    uint16_t flags = EV_ADD | EV_ENABLE | EV_CLEAR;
    if (oneshot) flags |= EV_ONESHOT;
    EV_SET(&kev, state->ident, EVFILT_TIMER, flags, 0, interval_ms, nullptr);
    while (::kevent(state->kq_fd, &kev, 1, nullptr, 0, nullptr) < 0 && errno == EINTR) {
    }
}

static void disarmTimerKqueue(const std::shared_ptr<PollWorkerTimerState>& state) {
    if (!state || state->kq_fd < 0) return;
    struct kevent kev;
    EV_SET(&kev, state->ident, EVFILT_TIMER, EV_DELETE, 0, 0, nullptr);
    while (::kevent(state->kq_fd, &kev, 1, nullptr, 0, nullptr) < 0 && errno == EINTR) {
    }
}

static uint64_t drainTimerEvents(const std::shared_ptr<PollWorkerTimerState>& state) {
    if (!state || state->kq_fd < 0) return 0;
    uint64_t total = 0;
    struct kevent events[16];
    struct timespec ts{0, 0};
    for (;;) {
        const int n = ::kevent(state->kq_fd, nullptr, 0, events, 16, &ts);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; ++i) {
            const auto& ev = events[i];
            if (ev.filter != EVFILT_TIMER || ev.ident != state->ident) continue;
            const uint64_t cnt = ev.data > 0 ? static_cast<uint64_t>(ev.data) : 1;
            total += cnt;
        }
        if (n < 16) break;
    }
    return total;
}

PollWorkerTimerWatcher::PollWorkerTimerWatcher(IOWorker& worker, std::function<void()> callback)
    : IWorkerWatcher(worker) {
    state_ = std::make_shared<PollWorkerTimerState>();
    state_->kq_fd = ::kqueue();
    if (state_->kq_fd < 0) {
        throw std::runtime_error(std::string("kqueue failed: ") + std::strerror(errno));
    }
    state_->cb = std::move(callback);
    createSource();
}

PollWorkerTimerWatcher::~PollWorkerTimerWatcher() {
    stop();
    clearSpec();
    source_.reset();
    state_.reset();
}

void PollWorkerTimerWatcher::start() {
    if (attached_) return;
    attachSource(source_);
    attached_ = true;
}

void PollWorkerTimerWatcher::stop() {
    if (!attached_) return;
    detachSource(source_);
    attached_ = false;
}

void PollWorkerTimerWatcher::setSpec(std::chrono::nanoseconds initial_delay,
                                     std::chrono::nanoseconds interval) {
    if (!state_ || state_->kq_fd < 0) return;
    if (initial_delay <= std::chrono::nanoseconds::zero() &&
        interval <= std::chrono::nanoseconds::zero()) {
        clearSpec();
        return;
    }

    const intptr_t initial_ms = toTimerMillis(initial_delay);
    const intptr_t period_ms = toTimerMillis(interval);

    state_->promote_to_periodic = false;
    state_->periodic_ms = 0;

    if (interval > std::chrono::nanoseconds::zero()) {
        if (initial_delay > std::chrono::nanoseconds::zero() && initial_delay != interval) {
            state_->promote_to_periodic = true;
            state_->periodic_ms = period_ms;
            armTimerKqueue(state_, initial_ms, true);
            return;
        }
        const intptr_t repeat_ms =
            initial_delay > std::chrono::nanoseconds::zero() ? initial_ms : period_ms;
        armTimerKqueue(state_, repeat_ms, false);
        return;
    }

    armTimerKqueue(state_, initial_ms, true);
}

void PollWorkerTimerWatcher::clearSpec() {
    if (!state_) return;
    state_->promote_to_periodic = false;
    state_->periodic_ms = 0;
    disarmTimerKqueue(state_);
}

void PollWorkerTimerWatcher::createSource() {
    auto state = state_;
    source_ = std::make_shared<PollReadinessEventSource>(
        state ? state->kq_fd : -1,
        POLLIN,
        [state](short events) {
            if (!(events & POLLIN)) return;
            if (!state || state->kq_fd < 0) return;
            const uint64_t expirations = drainTimerEvents(state);
            if (state->promote_to_periodic && state->periodic_ms > 0) {
                armTimerKqueue(state, state->periodic_ms, false);
                state->promote_to_periodic = false;
                state->periodic_ms = 0;
            }
            if (!state->cb || expirations == 0) return;
            for (uint64_t i = 0; i < expirations; ++i) {
                if (state->cb) state->cb();
            }
        });
}

} // namespace RopHive::MacOS

#endif // __APPLE__
