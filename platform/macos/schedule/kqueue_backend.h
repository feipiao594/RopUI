#ifndef _ROP_PLATFORM_MACOS_KQUEUE_EVENTLOOP_BACKEND_H
#define _ROP_PLATFORM_MACOS_KQUEUE_EVENTLOOP_BACKEND_H

#ifdef __APPLE__

#include <functional>
#include <unordered_map>
#include <vector>

#include <sys/event.h>

#include "../../schedule/eventloop_core.h"

namespace RopHive::MacOS {

struct KqueueRawEvent {
    uintptr_t ident;
    int16_t filter;
    uint16_t flags;
    uint32_t fflags;
    intptr_t data;
};

class KqueueEventSource : public IEventSource {
public:
    KqueueEventSource(int fd, int16_t filter);
    ~KqueueEventSource() override = default;

    void arm(IEventCoreBackend& backend) override;
    void disarm(IEventCoreBackend& backend) override;

    bool matches(const void* raw_event) const override;

protected:
    int fd() const { return fd_; }
    int16_t filter() const { return filter_; }
    const KqueueRawEvent* asKqueueEvent(const void* raw_event) const;

private:
    int fd_;
    int16_t filter_;
    bool armed_ = false;
};

class KqueueBackend final : public IEventCoreBackend {
public:
    explicit KqueueBackend(size_t max_events = 64);
    ~KqueueBackend() override;

    void addSource(IEventSource* source) override;
    void removeSource(IEventSource* source) override;

    void wait(int timeout) override;
    RawEventSpan rawEvents() const override;

    void registerReadFd(int fd);
    void unregisterReadFd(int fd);

private:
    int kq_{-1};
    size_t max_events_;
    std::unordered_map<int, bool> registered_read_;
    std::vector<struct kevent> events_;
    std::vector<KqueueRawEvent> ready_;
};

class KqueueEventLoopCore final : public IEventLoopCore {
public:
    KqueueEventLoopCore();
    ~KqueueEventLoopCore() override = default;
};

class KqueueReadinessEventSource final : public KqueueEventSource {
public:
    using Callback = std::function<void(const KqueueRawEvent&)>;

    explicit KqueueReadinessEventSource(int fd, Callback cb);
    void dispatch(const void* raw_event) override;

private:
    Callback cb_;
};

} // namespace RopHive::MacOS

#endif // __APPLE__

#endif // _ROP_PLATFORM_MACOS_KQUEUE_EVENTLOOP_BACKEND_H

