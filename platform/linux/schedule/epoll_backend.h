#ifndef _ROP_PLATFORM_LINUX_EPOLL_EVENTLOOP_BACKEND_H
#define _ROP_PLATFORM_LINUX_EPOLL_EVENTLOOP_BACKEND_H

#include <vector>
#include <functional>
#include <sys/epoll.h>

#include "../../eventloop_core.h"

namespace RopEventloop::Linux {

struct EpollRawEvent {
    int fd;
    uint32_t events;
};

class EpollEventSource : public IEventSource {
public:
    EpollEventSource(int fd, uint32_t events);
    ~EpollEventSource() override = default;

    void arm(IEventCoreBackend& backend) override;
    void disarm(IEventCoreBackend& backend) override;

    bool matches(const void* raw_event) const override;

protected:
        int fd() const { return fd_; }
    short events() const { return events_; }

    const EpollRawEvent* asEpollEvent(const void* raw_event) const;

private:
    int fd_;
    uint32_t events_;
    bool armed_ = false;
};

class EpollBackend final : public IEventCoreBackend {
public:
    EpollBackend(int vector_size = 64);
    ~EpollBackend() override;

    void addSource(IEventSource* source) override;
    void removeSource(IEventSource* source) override;

    void wait(int timeout) override;
    RawEventSpan rawEvents() const override;

    void registerFd(int fd, uint32_t events);
    void unregisterFd(int fd);

private:
    int epfd_;
    std::vector<epoll_event> epoll_events_;
    std::unordered_map<int, uint32_t> fd_events_;

    std::vector<EpollRawEvent> ready_events_;
};

class EpollEventLoopCore final : public IEventLoopCore {
public:
    EpollEventLoopCore();

    ~EpollEventLoopCore() override = default;
};

class EpollReadinessEventSource : public EpollEventSource {
public:
    using Callback = std::function<void(uint32_t)>;

    EpollReadinessEventSource(int fd,
                           uint32_t events,
                           Callback cb);

    void dispatch(const void* raw_event) override;

private:
    Callback callback_;
};

}

#endif // _ROP_PLATFORM_LINUX_EPOLL_EVENTLOOP_BACKEND_H
