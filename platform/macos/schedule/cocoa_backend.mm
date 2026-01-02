#include "cocoa_backend.h"

#ifdef __APPLE__

#import <AppKit/AppKit.h>
#include <CoreFoundation/CoreFoundation.h>

namespace RopHive::MacOS {

static void releaseReady(std::vector<CocoaRawEvent>& ready) {
    for (auto& e : ready) {
        if (e.event) {
            CFRelease((CFTypeRef)e.event);
            e.event = nullptr;
        }
    }
    ready.clear();
}

CocoaEventSource::CocoaEventSource(int type)
    : IEventSource(BackendType::MACOS_COCOA), type_(type) {}

void CocoaEventSource::arm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    armed_ = true;
}

void CocoaEventSource::disarm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    armed_ = false;
}

bool CocoaEventSource::matches(const void* raw_event) const {
    if (!armed_) return false;
    const auto* ev = static_cast<const CocoaRawEvent*>(raw_event);
    return ev->type == type_;
}

const CocoaRawEvent* CocoaEventSource::asCocoaEvent(const void* raw_event) const {
    return static_cast<const CocoaRawEvent*>(raw_event);
}

CocoaBackend::CocoaBackend()
    : IEventCoreBackend(BackendType::MACOS_COCOA) {
    // Must be created on the main thread in real usage.
    [NSApplication sharedApplication];
}

void CocoaBackend::addSource(IEventSource*) {}
void CocoaBackend::removeSource(IEventSource*) {}

void CocoaBackend::wait(int timeout) {
    releaseReady(ready_);

    @autoreleasepool {
        NSDate* untilDate = nil;
        if (timeout < 0) {
            untilDate = [NSDate distantFuture];
        } else {
            untilDate = [NSDate dateWithTimeIntervalSinceNow:(double)timeout / 1000.0];
        }

        NSEvent* ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                         untilDate:untilDate
                                            inMode:NSDefaultRunLoopMode
                                           dequeue:YES];
        if (!ev) {
            return;
        }

        // Keep the event alive across dispatch (runOnce() calls wait() then dispatch).
        CFRetain((__bridge CFTypeRef)ev);
        ready_.push_back(CocoaRawEvent{(int)[ev type], (void*)ev});
        [NSApp sendEvent:ev];
        [NSApp updateWindows];

        // Drain remaining queued events without blocking.
        while (true) {
            NSEvent* more = [NSApp nextEventMatchingMask:NSEventMaskAny
                                              untilDate:[NSDate distantPast]
                                                 inMode:NSDefaultRunLoopMode
                                                dequeue:YES];
            if (!more) break;
            CFRetain((__bridge CFTypeRef)more);
            ready_.push_back(CocoaRawEvent{(int)[more type], (void*)more});
            [NSApp sendEvent:more];
            [NSApp updateWindows];
        }
    }
}

RawEventSpan CocoaBackend::rawEvents() const {
    return RawEventSpan{
        .data = ready_.data(),
        .count = ready_.size(),
        .stride = sizeof(CocoaRawEvent),
    };
}

CocoaEventLoopCore::CocoaEventLoopCore()
    : IEventLoopCore(std::make_unique<CocoaBackend>()) {}

CocoaEventTypeSource::CocoaEventTypeSource(int type, Callback cb)
    : CocoaEventSource(type),
      cb_(std::move(cb)) {}

void CocoaEventTypeSource::dispatch(const void* raw_event) {
    if (!cb_) return;
    cb_(*asCocoaEvent(raw_event));
}

} // namespace RopHive::MacOS

#endif // __APPLE__

