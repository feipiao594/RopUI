#ifndef _ROP_POLL_PLATFORM_EVENTLOOP__BACKEND_H
#define _ROP_POLL_PLATFORM_EVENTLOOP__BACKEND_H

#include <vector>
#include <unordered_map>
#include <poll.h>

#include "../../eventloop.h"

namespace RopEventloop {

struct LinuxPollRawEvent {
    int fd;
    short revents;
};

class LinuxPollEventSource : public IEventSource {
public:
    explicit LinuxPollEventSource(int fd, short events);
    ~LinuxPollEventSource() override = default;

    void arm(IEventCoreBackend& backend) override;
    void disarm(IEventCoreBackend& backend) override;

    bool matches(const void* raw_event) const override;

protected:
    int fd() const { return fd_; }
    short events() const { return events_; }

    const LinuxPollRawEvent* asPollEvent(const void* raw_event) const;

private:
    int fd_;
    short events_;
    bool armed_ = false;
};

class LinuxPollBackend final : public IEventCoreBackend {
public:
    LinuxPollBackend();
    ~LinuxPollBackend() override = default;

    void addSource(IEventSource* source) override;
    void removeSource(IEventSource* source) override;

    void wait() override;

    RawEventSpan rawEvents() const override;

    // poll-specific helper API (used by EventSource)
    void registerFd(int fd, short events);
    void unregisterFd(int fd);

private:
    std::vector<pollfd> pollfds_;
    std::unordered_map<int, size_t> fd_index_;

    std::vector<LinuxPollRawEvent> ready_events_;
};


class LinuxPollEventLoopCore final : public IEventLoopCore {
public:
    LinuxPollEventLoopCore();

    ~LinuxPollEventLoopCore() override = default;
};


class SocketEventSource final : public LinuxPollEventSource {
public:
    using Callback = std::function<void(short revents)>;

    SocketEventSource(int fd,
                      short events,
                      Callback cb);

    void dispatch(const void* raw_event) override;

private:
    Callback callback_;
};

}

#endif // _ROP_POLL_PLATFORM_EVENTLOOP__BACKEND_H