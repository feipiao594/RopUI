#include <cassert>
#include <errno.h>
#include <cstring>

#include <log.hpp>

#include "poll_backend.h"
#include "schedule/eventloop_core.h"


namespace RopEventloop::MacOS {

PollEventSource::PollEventSource(int fd, short events)
    : IEventSource(BackendType::MACOS_POLL), fd_(fd), events_(events) {}

void PollEventSource::arm(IEventCoreBackend& backend) {
    if(!isSourceMatchBackend(&backend)) {
        LOG(WARN)("Source arm to a dismatch backend");
        return;
    }
    if (armed_) {
        LOG(WARN)("try arm fd %d but it already armed", fd_);
        return;
    }

    auto& poll_backend = static_cast<PollBackend&>(backend);
    poll_backend.registerFd(fd_, events_);
    armed_ = true;
}

void PollEventSource::disarm(IEventCoreBackend& backend) {
    if(!isSourceMatchBackend(&backend)) {
        LOG(WARN)("Source disarm to a dismatch backend");
        return;
    }
    if (!armed_) {
        LOG(WARN)("try disarm fd %d but it not be armed", fd_);
        return;
    }

    auto& poll_backend = static_cast<PollBackend&>(backend);
    poll_backend.unregisterFd(fd_);
    armed_ = false;
}

bool PollEventSource::matches(const void* raw_event) const {
    auto* ev = static_cast<const PollRawEvent*>(raw_event);
    return ev->fd == fd_;
}

const PollRawEvent*
PollEventSource::asPollEvent(const void* raw_event) const {
    return static_cast<const PollRawEvent*>(raw_event);
}

PollBackend::PollBackend() : IEventCoreBackend(BackendType::LINUX_POLL) {};

void PollBackend::addSource(IEventSource*) {}

void PollBackend::removeSource(IEventSource*) {}

void PollBackend::registerFd(int fd, short events) {
    LOG(DEBUG)("registering fd %d with events 0x%04x", fd, events);
    auto it = fd_index_.find(fd);
    if (it != fd_index_.end()) {
        pollfds_[it->second].events = events;
        return;
    }

    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;

    fd_index_[fd] = pollfds_.size();
    pollfds_.push_back(pfd);
}

void PollBackend::unregisterFd(int fd) {
    auto it = fd_index_.find(fd);
    if (it == fd_index_.end()) {
        LOG(WARN)("try unregister fd %d but it not registered", fd);
        return;
    }

    size_t index = it->second;
    size_t last = pollfds_.size() - 1;

    if (index != last) {
        pollfds_[index] = pollfds_[last];
        fd_index_[pollfds_[index].fd] = index;
    }

    pollfds_.pop_back();
    fd_index_.erase(it);
}

void PollBackend::wait(int timeout) {
    ready_events_.clear();

    LOG(DEBUG)("poll waiting on %zu fds", pollfds_.size());

    int ret;
    do {
        ret = ::poll(pollfds_.empty() ? nullptr : pollfds_.data(),
                     static_cast<nfds_t>(pollfds_.size()),
                     timeout);
    } while (ret < 0 && errno == EINTR);

    if (ret <= 0) {
        return;
    }

    int num_found = 0;
    for (const auto& pfd : pollfds_) {
        if (pfd.revents != 0) {
            if (pfd.revents & POLLNVAL) {
#ifdef ROPUI_DEBUG
                LOG(ERROR)("POLLNVAL detected on fd %d", pfd.fd);
                unregisterFd(pfd.fd); 
#else
                assert(!(pfd.revents & POLLNVAL));
#endif    
            }

            ready_events_.push_back(PollRawEvent{pfd.fd, pfd.revents});
            
            if (++num_found >= ret) {
                break;
            }
        }
    }
}


RawEventSpan PollBackend::rawEvents() const {
    return RawEventSpan{
        .data   = ready_events_.data(),
        .count  = ready_events_.size(),
        .stride = sizeof(PollRawEvent)
    };
}

PollEventLoopCore::PollEventLoopCore()
    : IEventLoopCore(std::make_unique<PollBackend>()) {
}


PollReadinessEventSource::PollReadinessEventSource(int fd,
                                     short events,
                                     Callback cb)
    : PollEventSource(fd, events),
      callback_(std::move(cb)) {}

void PollReadinessEventSource::dispatch(const void* raw_event) {
    const PollRawEvent* ev = asPollEvent(raw_event);

    if (callback_) {
        callback_(ev->revents);
    }
}

}