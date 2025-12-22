#ifndef _ROP_PLATFORM_LINUX_POLL_EVENTLOOP_BACKEND_H
#define _ROP_PLATFORM_LINUX_POLL_EVENTLOOP_BACKEND_H

#include <vector>
#include <functional>
#include <unordered_map>
#include <poll.h>

#include "../../schedule/eventloop_core.h"


namespace RopEventloop::Linux {

struct PollRawEvent {
    int fd;
    short revents;
};

class PollEventSource : public IEventSource {
public:
    explicit PollEventSource(int fd, short events);
    ~PollEventSource() override = default;

    void arm(IEventCoreBackend& backend) override;
    void disarm(IEventCoreBackend& backend) override;

    bool matches(const void* raw_event) const override;

protected:
    int fd() const { return fd_; }
    short events() const { return events_; }

    const PollRawEvent* asPollEvent(const void* raw_event) const;

private:
    int fd_;
    short events_;
    bool armed_ = false;
};

class PollBackend final : public IEventCoreBackend {
public:
    PollBackend();
    ~PollBackend() override = default;

    void addSource(IEventSource* source) override;
    void removeSource(IEventSource* source) override;

    void wait(int timeout) override;

    RawEventSpan rawEvents() const override;

    // poll-specific helper API (used by EventSource)
    void registerFd(int fd, short events);
    void unregisterFd(int fd);

private:
    std::vector<pollfd> pollfds_;
    std::unordered_map<int, size_t> fd_index_;

    std::vector<PollRawEvent> ready_events_;
};


class PollEventLoopCore final : public IEventLoopCore {
public:
    PollEventLoopCore();

    ~PollEventLoopCore() override = default;
};


class PollReadinessEventSource : public PollEventSource {
public:
    using Callback = std::function<void(short revents)>;

    PollReadinessEventSource(int fd,
                      short events,
                      Callback cb);

    void dispatch(const void* raw_event) override;

private:
    Callback callback_;
};

}

#endif // _ROP_PLATFORM_LINUX_POLL_EVENTLOOP_BACKEND_H