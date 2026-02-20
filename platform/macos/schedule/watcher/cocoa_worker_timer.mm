#ifdef __APPLE__

#import <AppKit/AppKit.h>
#include <dispatch/dispatch.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <stdexcept>

#include "cocoa_worker_timer.h"

#include "../cocoa_backend.h"

namespace RopHive::MacOS {

static constexpr int kAppDefinedType = (int)NSEventTypeApplicationDefined;
static constexpr short kTimerSubtype = 201;
static std::atomic<NSInteger> gTimerToken{1};

class CocoaAppDefinedEventSource final : public IEventSource {
public:
    using Callback = std::function<void(const CocoaRawEvent&)>;

    CocoaAppDefinedEventSource(short subtype, NSInteger token, Callback cb)
        : IEventSource(BackendType::MACOS_COCOA),
          subtype_(subtype),
          token_(token),
          cb_(std::move(cb)) {}

    void arm(IEventCoreBackend& backend) override {
        if (!isSourceMatchBackend(&backend)) return;
        armed_ = true;
    }

    void disarm(IEventCoreBackend& backend) override {
        if (!isSourceMatchBackend(&backend)) return;
        armed_ = false;
    }

    bool matches(const void* raw_event) const override {
        if (!armed_ || !raw_event) return false;
        const auto* ev = static_cast<const CocoaRawEvent*>(raw_event);
        if (!ev->event || ev->type != kAppDefinedType) return false;
        NSEvent* e = (__bridge NSEvent*)ev->event;
        return [e subtype] == subtype_ && [e data1] == token_;
    }

    void dispatch(const void* raw_event) override {
        if (!cb_ || !raw_event) return;
        cb_(*static_cast<const CocoaRawEvent*>(raw_event));
    }

private:
    short subtype_;
    NSInteger token_;
    Callback cb_;
    bool armed_{false};
};

static void postTimerEvent(short subtype, NSInteger data1, NSInteger data2) {
    [NSApplication sharedApplication];

    NSEvent* ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                     location:NSZeroPoint
                                modifierFlags:0
                                    timestamp:0
                                 windowNumber:0
                                      context:nil
                                      subtype:subtype
                                        data1:data1
                                        data2:data2];
    [NSApp postEvent:ev atStart:NO];
}

static uint64_t toNs(std::chrono::nanoseconds d) {
    if (d <= std::chrono::nanoseconds::zero()) return 1;
    return static_cast<uint64_t>(d.count());
}

struct CocoaWorkerTimerState {
    dispatch_source_t timer_source{nullptr};
    bool timer_resumed{false};
    short subtype{kTimerSubtype};
    NSInteger token{0};
    std::function<void()> cb{nullptr};

    ~CocoaWorkerTimerState() {
        if (!timer_source) return;
        dispatch_source_cancel(timer_source);
        if (!timer_resumed) {
            dispatch_resume(timer_source);
            timer_resumed = true;
        }
#if !OS_OBJECT_USE_OBJC
        dispatch_release(timer_source);
#endif
        timer_source = nullptr;
    }
};

CocoaWorkerTimerWatcher::CocoaWorkerTimerWatcher(IOWorker& worker, std::function<void()> callback)
    : IWorkerWatcher(worker) {
    state_ = std::make_shared<CocoaWorkerTimerState>();
    state_->token = gTimerToken.fetch_add(1, std::memory_order_relaxed);
    state_->cb = std::move(callback);
    state_->timer_source = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_TIMER,
        0,
        0,
        dispatch_get_main_queue());
    if (!state_->timer_source) {
        throw std::runtime_error("dispatch_source_create timer failed");
    }

    auto state = state_;
    dispatch_source_set_event_handler(state_->timer_source, ^{
        if (!state || !state->timer_source) return;
        uint64_t expirations = dispatch_source_get_data(state->timer_source);
        if (expirations == 0) expirations = 1;

        constexpr uint64_t kMax = static_cast<uint64_t>(std::numeric_limits<NSInteger>::max());
        const NSInteger data2 = static_cast<NSInteger>(std::min<uint64_t>(expirations, kMax));
        postTimerEvent(state->subtype, state->token, data2);
    });

    createSource();
}

CocoaWorkerTimerWatcher::~CocoaWorkerTimerWatcher() {
    stop();
    source_.reset();
    state_.reset();
}

void CocoaWorkerTimerWatcher::start() {
    if (attached_) return;
    attachSource(source_);
    attached_ = true;
}

void CocoaWorkerTimerWatcher::stop() {
    if (!attached_) return;
    clearSpec();
    detachSource(source_);
    attached_ = false;
}

void CocoaWorkerTimerWatcher::setSpec(std::chrono::nanoseconds initial_delay,
                                      std::chrono::nanoseconds interval) {
    if (!state_ || !state_->timer_source) return;
    if (initial_delay <= std::chrono::nanoseconds::zero() &&
        interval <= std::chrono::nanoseconds::zero()) {
        clearSpec();
        return;
    }

    const uint64_t start_ns =
        interval > std::chrono::nanoseconds::zero()
            ? toNs(initial_delay > std::chrono::nanoseconds::zero() ? initial_delay : interval)
            : toNs(initial_delay);

    const uint64_t repeat_ns =
        interval > std::chrono::nanoseconds::zero()
            ? toNs(interval)
            : DISPATCH_TIME_FOREVER;

    const uint64_t leeway_ns =
        repeat_ns == DISPATCH_TIME_FOREVER
            ? 1000000ULL
            : std::max<uint64_t>(1, std::min<uint64_t>(repeat_ns / 10, 1000000ULL));

    dispatch_source_set_timer(
        state_->timer_source,
        dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(start_ns)),
        repeat_ns,
        leeway_ns);

    if (!state_->timer_resumed) {
        dispatch_resume(state_->timer_source);
        state_->timer_resumed = true;
    }
}

void CocoaWorkerTimerWatcher::clearSpec() {
    if (!state_ || !state_->timer_source) return;
    dispatch_source_set_timer(
        state_->timer_source,
        DISPATCH_TIME_FOREVER,
        DISPATCH_TIME_FOREVER,
        0);
}

void CocoaWorkerTimerWatcher::createSource() {
    auto state = state_;
    source_ = std::make_shared<CocoaAppDefinedEventSource>(
        state ? state->subtype : kTimerSubtype,
        state ? state->token : 0,
        [state](const CocoaRawEvent& raw) {
            if (!state || !state->cb || !raw.event) return;
            NSEvent* ev = (__bridge NSEvent*)raw.event;
            const NSInteger data2 = [ev data2];
            const uint64_t expirations = data2 > 0 ? static_cast<uint64_t>(data2) : 1;
            for (uint64_t i = 0; i < expirations; ++i) {
                if (state->cb) state->cb();
            }
        });
}

} // namespace RopHive::MacOS

#endif // __APPLE__
