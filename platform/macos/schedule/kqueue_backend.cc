#include "kqueue_backend.h"

#ifdef __APPLE__

#include <unistd.h>

namespace RopHive::MacOS {

KqueueEventSource::KqueueEventSource(int fd, int16_t filter)
    : IEventSource(BackendType::MACOS_KQUEUE),
      fd_(fd),
      filter_(filter) {}

void KqueueEventSource::arm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    if (armed_) return;
    auto& kq = static_cast<KqueueBackend&>(backend);
    if (filter_ == EVFILT_READ) {
        kq.registerReadFd(fd_);
    }
    armed_ = true;
}

void KqueueEventSource::disarm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    if (!armed_) return;
    auto& kq = static_cast<KqueueBackend&>(backend);
    if (filter_ == EVFILT_READ) {
        kq.unregisterReadFd(fd_);
    }
    armed_ = false;
}

bool KqueueEventSource::matches(const void* raw_event) const {
    const auto* ev = static_cast<const KqueueRawEvent*>(raw_event);
    return ev->ident == static_cast<uintptr_t>(fd_) && ev->filter == filter_;
}

const KqueueRawEvent* KqueueEventSource::asKqueueEvent(const void* raw_event) const {
    return static_cast<const KqueueRawEvent*>(raw_event);
}

KqueueBackend::KqueueBackend(size_t max_events)
    : IEventCoreBackend(BackendType::MACOS_KQUEUE),
      kq_(::kqueue()),
      max_events_(max_events),
      events_(max_events) {}

KqueueBackend::~KqueueBackend() {
    if (kq_ >= 0) {
        ::close(kq_);
        kq_ = -1;
    }
}

void KqueueBackend::addSource(IEventSource*) {}
void KqueueBackend::removeSource(IEventSource*) {}

void KqueueBackend::registerReadFd(int fd) {
    if (kq_ < 0) return;
    if (registered_read_.find(fd) != registered_read_.end()) {
        return;
    }
    struct kevent kev;
    EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, nullptr);
    ::kevent(kq_, &kev, 1, nullptr, 0, nullptr);
    registered_read_[fd] = true;
}

void KqueueBackend::unregisterReadFd(int fd) {
    if (kq_ < 0) return;
    auto it = registered_read_.find(fd);
    if (it == registered_read_.end()) {
        return;
    }
    struct kevent kev;
    EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    ::kevent(kq_, &kev, 1, nullptr, 0, nullptr);
    registered_read_.erase(it);
}

void KqueueBackend::wait(int timeout) {
    ready_.clear();
    if (kq_ < 0) return;

    struct timespec ts;
    struct timespec* pts = nullptr;
    if (timeout >= 0) {
        ts.tv_sec = timeout / 1000;
        ts.tv_nsec = (timeout % 1000) * 1000000;
        pts = &ts;
    }

    const int n = ::kevent(kq_, nullptr, 0, events_.data(), static_cast<int>(max_events_), pts);
    if (n <= 0) return;

    ready_.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const auto& ev = events_[i];
        ready_.push_back(KqueueRawEvent{
            ev.ident,
            ev.filter,
            ev.flags,
            ev.fflags,
            ev.data,
        });
    }
}

RawEventSpan KqueueBackend::rawEvents() const {
    return RawEventSpan{
        .data = ready_.data(),
        .count = ready_.size(),
        .stride = sizeof(KqueueRawEvent),
    };
}

KqueueEventLoopCore::KqueueEventLoopCore()
    : IEventLoopCore(std::make_unique<KqueueBackend>()) {}

KqueueReadinessEventSource::KqueueReadinessEventSource(int fd, Callback cb)
    : KqueueEventSource(fd, EVFILT_READ),
      cb_(std::move(cb)) {}

void KqueueReadinessEventSource::dispatch(const void* raw_event) {
    if (!cb_) return;
    cb_(*asKqueueEvent(raw_event));
}

} // namespace RopHive::MacOS

#endif // __APPLE__

