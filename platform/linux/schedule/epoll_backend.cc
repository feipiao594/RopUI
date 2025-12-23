
#include <unistd.h>
#include <errno.h>

#include <log.hpp>

#include "epoll_backend.h"
#include "schedule/eventloop_core.h"

namespace RopEventloop::Linux {

EpollEventSource::EpollEventSource(int fd, uint32_t events)
    : IEventSource(BackendType::LINUX_EPOLL), fd_(fd), events_(events) {}

void EpollEventSource::arm(IEventCoreBackend& backend) {
    if(!isSourceMatchBackend(&backend)) {
        LOG(WARN)("Source arm to a dismatch backend");
        return;
    }
    if (armed_) return;
    static_cast<EpollBackend&>(backend).registerFd(fd_, events_);
    armed_ = true;
}

void EpollEventSource::disarm(IEventCoreBackend& backend) {
    if(!isSourceMatchBackend(&backend)) {
        LOG(WARN)("Source disarm to a dismatch backend");
        return;
    }
    if (!armed_) return;
    static_cast<EpollBackend&>(backend).unregisterFd(fd_);
    armed_ = false;
}

bool EpollEventSource::matches(const void* raw_event) const {
    auto* ev = static_cast<const EpollRawEvent*>(raw_event);
    return ev->fd == fd_;
}

const EpollRawEvent*
EpollEventSource::asEpollEvent(const void* raw_event) const {
    return static_cast<const EpollRawEvent*>(raw_event);
}

EpollBackend::EpollBackend(int vector_size)
    : IEventCoreBackend(BackendType::LINUX_EPOLL), 
      epfd_(::epoll_create1(EPOLL_CLOEXEC)),
      epoll_events_(vector_size) {}

EpollBackend::~EpollBackend() {
    ::close(epfd_);
}

void EpollBackend::addSource(IEventSource*) {}

void EpollBackend::removeSource(IEventSource*) {}

void EpollBackend::registerFd(int fd, uint32_t events) {
    auto it = fd_events_.find(fd);

    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (it == fd_events_.end()) {
        if (::epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) == 0) {
            fd_events_[fd] = events;
        }
        return;
    }

    // exist but events not changed, skip
    if (it->second == events) {
        return;
    }

    if (::epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) == 0) {
        it->second = events;
    }
}

void EpollBackend::unregisterFd(int fd) {
    auto it = fd_events_.find(fd);
    if (it == fd_events_.end()) return;

    ::epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, nullptr);
    fd_events_.erase(it);
}


void EpollBackend::wait(int timeout) {
    ready_events_.clear();

    int n;
    do {
        n = ::epoll_wait(epfd_,
                         epoll_events_.data(),
                         static_cast<int>(epoll_events_.size()),
                         timeout);
        // LOG(DEBUG)("epoll wait one loop");
    } while (n < 0 && errno == EINTR);

    if (n <= 0) return;

    for (int i = 0; i < n; ++i) {
        ready_events_.push_back({
            epoll_events_[i].data.fd,
            epoll_events_[i].events
        });
    }
}

RawEventSpan EpollBackend::rawEvents() const {
    return RawEventSpan{
        .data   = ready_events_.data(),
        .count  = ready_events_.size(),
        .stride = sizeof(EpollRawEvent)
    };
}

EpollEventLoopCore::EpollEventLoopCore()
    : IEventLoopCore(std::make_unique<EpollBackend>()) {}

EpollReadinessEventSource::EpollReadinessEventSource(
    int fd,
    uint32_t events,
    Callback cb)
    : EpollEventSource(fd, events),
      callback_(std::move(cb)) {}

void EpollReadinessEventSource::dispatch(const void* raw_event) {
    const auto* ev = asEpollEvent(raw_event);
    if (callback_) {
        callback_(ev->events);
    }
}

}
