#include "poll_eventloop.h"

#include <cassert>
#include <errno.h>
#include <cstring>

namespace RopEventloop {

LinuxPollEventSource::LinuxPollEventSource(int fd, short events)
    : fd_(fd), events_(events) {}

void LinuxPollEventSource::arm(IEventCoreBackend& backend) {
    if (armed_) {
        return;
    }

    auto& poll_backend = static_cast<LinuxPollBackend&>(backend);
    poll_backend.registerFd(fd_, events_);
    armed_ = true;
}

void LinuxPollEventSource::disarm(IEventCoreBackend& backend) {
    if (!armed_) {
        return;
    }

    auto& poll_backend = static_cast<LinuxPollBackend&>(backend);
    poll_backend.unregisterFd(fd_);
    armed_ = false;
}

bool LinuxPollEventSource::matches(const void* raw_event) const {
    auto* ev = static_cast<const LinuxPollRawEvent*>(raw_event);
    return ev->fd == fd_;
}

const LinuxPollRawEvent*
LinuxPollEventSource::asPollEvent(const void* raw_event) const {
    return static_cast<const LinuxPollRawEvent*>(raw_event);
}

LinuxPollBackend::LinuxPollBackend() = default;

void LinuxPollBackend::addSource(IEventSource* /*source*/) {
    // 幂等：LinuxPollBackend 不需要在这里做任何事
    // source 的实际 fd 注册由 source->arm() 完成
}

void LinuxPollBackend::removeSource(IEventSource* /*source*/) {
    // 幂等：source->disarm() 应该已经清理了 fd
}

void LinuxPollBackend::registerFd(int fd, short events) {
    auto it = fd_index_.find(fd);
    if (it != fd_index_.end()) {
        // 已存在：更新监听事件
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

void LinuxPollBackend::unregisterFd(int fd) {
    auto it = fd_index_.find(fd);
    if (it == fd_index_.end()) {
        // 不存在：幂等 no-op
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

void LinuxPollBackend::wait() {
    ready_events_.clear();

    if (pollfds_.empty()) {
        // 没有 fd，避免 busy-loop，直接阻塞一点
        ::poll(nullptr, 0, -1);
        return;
    }

    int ret;
    do {
        ret = ::poll(pollfds_.data(),
                     static_cast<nfds_t>(pollfds_.size()),
                     -1);
    } while (ret < 0 && errno == EINTR);

    if (ret <= 0) {
        return;
    }

    for (const auto& pfd : pollfds_) {
        if (pfd.revents != 0) {
            ready_events_.push_back(
                LinuxPollRawEvent{pfd.fd, pfd.revents});
        }
    }
}

RawEventSpan LinuxPollBackend::rawEvents() const {
    return RawEventSpan{
        .data   = ready_events_.data(),
        .count  = ready_events_.size(),
        .stride = sizeof(LinuxPollRawEvent)
    };
}

LinuxPollEventLoopCore::LinuxPollEventLoopCore()
    : IEventLoopCore(std::make_unique<LinuxPollBackend>()) {
}


SocketEventSource::SocketEventSource(int fd,
                                     short events,
                                     Callback cb)
    : LinuxPollEventSource(fd, events),
      callback_(std::move(cb)) {}

void SocketEventSource::dispatch(const void* raw_event) {
    const LinuxPollRawEvent* ev = asPollEvent(raw_event);

    if (callback_) {
        callback_(ev->revents);
    }
}


}