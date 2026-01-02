#ifdef __APPLE__

#import <AppKit/AppKit.h>

#include "cocoa_worker_wakeup.h"

#include "../cocoa_backend.h"

namespace RopHive::MacOS {

static constexpr int kWakeEventType = (int)NSEventTypeApplicationDefined;

CocoaWorkerWakeUpWatcher::CocoaWorkerWakeUpWatcher(IOWorker& worker)
    : IWorkerWakeUpWatcher(worker) {
    source_ = std::make_unique<CocoaEventTypeSource>(
        kWakeEventType,
        [](const CocoaRawEvent&) {
            // no-op: just drain wake events
        });
}

CocoaWorkerWakeUpWatcher::~CocoaWorkerWakeUpWatcher() {
    stop();
}

void CocoaWorkerWakeUpWatcher::start() {
    if (attached_) return;
    attachSource(source_.get());
    attached_ = true;
}

void CocoaWorkerWakeUpWatcher::stop() {
    if (!attached_) return;
    detachSource(source_.get());
    attached_ = false;
}

void CocoaWorkerWakeUpWatcher::notify() {
    if (!attached_) return;
    [NSApplication sharedApplication];

    NSEvent* ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                     location:NSZeroPoint
                                modifierFlags:0
                                    timestamp:0
                                 windowNumber:0
                                      context:nil
                                      subtype:0
                                        data1:0
                                        data2:0];
    [NSApp postEvent:ev atStart:NO];
}

} // namespace RopHive::MacOS

#endif // __APPLE__

