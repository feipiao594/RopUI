#include "kqueue_backend.h"

#ifdef __APPLE__

#include <unistd.h>

namespace RopHive::MacOS {

static uint64_t registrationKey(int fd, int16_t filter) {
    // Pack (fd, filter) into a 64-bit key. fd is assumed to fit into 32 bits.
    const uint64_t ufd = static_cast<uint32_t>(fd);
    const uint64_t ufilter = static_cast<uint16_t>(filter);
    return (ufd << 32) | ufilter;
}

KqueueEventSource::KqueueEventSource(int fd,
                                     int16_t filter,
                                     uint16_t flags,
                                     uint32_t fflags,
                                     intptr_t data)
    : IEventSource(BackendType::MACOS_KQUEUE),
      fd_(fd),
      filter_(filter),
      flags_(flags),
      fflags_(fflags),
      data_(data) {}

void KqueueEventSource::arm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    if (armed_) return;
    auto& kq = static_cast<KqueueBackend&>(backend);
    kq.registerFd(fd_, filter_, flags_, fflags, data_);
    armed_ = true;
}

void KqueueEventSource::disarm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    if (!armed_) return;
    auto& kq = static_cast<KqueueBackend&>(backend);
    kq.unregisterFd(fd_, filter_);
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

void KqueueBackend::registerFd(int fd,
                               int16_t filter,
                               uint16_t flags,
                               uint32_t fflags,
                               intptr_t data) {
    if (kq_ < 0) return;
    const uint64_t key = registrationKey(fd, filter);
    const Registration reg{flags, fflags, data};
    const auto it = registered_.find(key);
    if (it != registered_.end()) {
        if (it->second.flags == reg.flags && it->second.fflags == reg.fflags && it->second.data == reg.data) {
            return;
        }
    }
    struct kevent kev;
    EV_SET(&kev, fd, filter, flags, fflags, data, nullptr);
    ::kevent(kq_, &kev, 1, nullptr, 0, nullptr);
    registered_[key] = reg;
}

void KqueueBackend::unregisterFd(int fd, int16_t filter) {
    if (kq_ < 0) return;
    const uint64_t key = registrationKey(fd, filter);
    const auto it = registered_.find(key);
    if (it == registered_.end()) {
        return;
    }
    struct kevent kev;
    EV_SET(&kev, fd, filter, EV_DELETE, 0, 0, nullptr);
    ::kevent(kq_, &kev, 1, nullptr, 0, nullptr);
    registered_.erase(it);
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

KqueueReadinessEventSource::KqueueReadinessEventSource(int fd, int16_t filter, Callback cb)
    : KqueueEventSource(fd, filter),
      cb_(std::move(cb)) {}

KqueueReadinessEventSource::KqueueReadinessEventSource(int fd,
                                                       int16_t filter,
                                                       uint16_t flags,
                                                       uint32_t fflags,
                                                       intptr_t data,
                                                       Callback cb)
    : KqueueEventSource(fd, filter, flags, fflags, data),
      cb_(std::move(cb)) {}

void KqueueReadinessEventSource::dispatch(const void* raw_event) {
    if (!cb_) return;
    cb_(*asKqueueEvent(raw_event));
}

} // namespace RopHive::MacOS

#endif // __APPLE__
