#ifndef _ROP_PLATFORM_MACOS_COCOA_EVENTLOOP_BACKEND_H
#define _ROP_PLATFORM_MACOS_COCOA_EVENTLOOP_BACKEND_H

#ifdef __APPLE__

#include <functional>
#include <vector>

#include "../../schedule/eventloop_core.h"

namespace RopHive::MacOS {

struct CocoaRawEvent {
    int type;
    void* event; // NSEvent* (stored as void* to keep this header C++-compatible)
};

class CocoaEventSource : public IEventSource {
public:
    explicit CocoaEventSource(int type);
    ~CocoaEventSource() override = default;

    void arm(IEventCoreBackend& backend) override;
    void disarm(IEventCoreBackend& backend) override;

    bool matches(const void* raw_event) const override;

protected:
    int type() const { return type_; }
    const CocoaRawEvent* asCocoaEvent(const void* raw_event) const;

private:
    int type_;
    bool armed_ = false;
};

// Cocoa/UI backend based on NSApplication event pump.
class CocoaBackend final : public IEventCoreBackend {
public:
    CocoaBackend();
    ~CocoaBackend() override = default;

    void addSource(IEventSource* source) override;
    void removeSource(IEventSource* source) override;

    void wait(int timeout) override;
    RawEventSpan rawEvents() const override;

private:
    std::vector<CocoaRawEvent> ready_;
};

class CocoaEventLoopCore final : public IEventLoopCore {
public:
    CocoaEventLoopCore();
    ~CocoaEventLoopCore() override = default;
};

class CocoaEventTypeSource final : public CocoaEventSource {
public:
    using Callback = std::function<void(const CocoaRawEvent&)>;

    CocoaEventTypeSource(int type, Callback cb);
    void dispatch(const void* raw_event) override;

private:
    Callback cb_;
};

} // namespace RopHive::MacOS

#endif // __APPLE__

#endif // _ROP_PLATFORM_MACOS_COCOA_EVENTLOOP_BACKEND_H
