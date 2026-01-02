#include "iocp_backend.h"

#ifdef _WIN32

namespace RopHive::Windows {

IocpEventSource::IocpEventSource(ULONG_PTR key)
    : IEventSource(BackendType::WINDOWS_IOCP), key_(key) {}

void IocpEventSource::arm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    armed_ = true;
}

void IocpEventSource::disarm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    armed_ = false;
}

bool IocpEventSource::matches(const void* raw_event) const {
    if (!armed_) return false;
    const auto* ev = static_cast<const IocpRawEvent*>(raw_event);
    return ev->key == key_;
}

const IocpRawEvent* IocpEventSource::asIocpEvent(const void* raw_event) const {
    return static_cast<const IocpRawEvent*>(raw_event);
}

IocpBackend::IocpBackend(size_t max_events)
    : IEventCoreBackend(BackendType::WINDOWS_IOCP),
      port_(::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0)),
      max_events_(max_events) {
}

IocpBackend::~IocpBackend() {
    if (port_) {
        ::CloseHandle(port_);
        port_ = nullptr;
    }
}

void IocpBackend::addSource(IEventSource*) {}
void IocpBackend::removeSource(IEventSource*) {}

void IocpBackend::postWake(ULONG_PTR key) {
    ::PostQueuedCompletionStatus(port_, 0, key, nullptr);
}

void IocpBackend::wait(int timeout) {
    ready_.clear();

    DWORD ms = (timeout < 0) ? INFINITE : static_cast<DWORD>(timeout);
    std::vector<OVERLAPPED_ENTRY> entries(max_events_);

    ULONG removed = 0;
    BOOL ok = ::GetQueuedCompletionStatusEx(
        port_,
        entries.data(),
        static_cast<ULONG>(entries.size()),
        &removed,
        ms,
        FALSE);

    if (!ok) {
        const DWORD err = ::GetLastError();
        if (err == WAIT_TIMEOUT) {
            return;
        }
        return;
    }

    ready_.reserve(removed);
    for (ULONG i = 0; i < removed; ++i) {
        ready_.push_back(IocpRawEvent{
            entries[i].lpCompletionKey,
            entries[i].lpOverlapped,
            entries[i].dwNumberOfBytesTransferred,
            0,
        });
    }
}

RawEventSpan IocpBackend::rawEvents() const {
    return RawEventSpan{
        .data = ready_.data(),
        .count = ready_.size(),
        .stride = sizeof(IocpRawEvent),
    };
}

IocpEventLoopCore::IocpEventLoopCore()
    : IEventLoopCore(std::make_unique<IocpBackend>()) {}

IocpCompletionEventSource::IocpCompletionEventSource(ULONG_PTR key, Callback cb)
    : IocpEventSource(key),
      cb_(std::move(cb)) {}

void IocpCompletionEventSource::dispatch(const void* raw_event) {
    if (!cb_) return;
    cb_(*asIocpEvent(raw_event));
}

IocpWakeUpEventSource::IocpWakeUpEventSource(ULONG_PTR key)
    : IocpEventSource(key) {}

void IocpWakeUpEventSource::arm(IEventCoreBackend& backend) {
    IocpEventSource::arm(backend);
    if (!isSourceMatchBackend(&backend)) {
        backend_ = nullptr;
        return;
    }
    backend_ = static_cast<IocpBackend*>(&backend);
}

void IocpWakeUpEventSource::disarm(IEventCoreBackend& backend) {
    IocpEventSource::disarm(backend);
    (void)backend;
    backend_ = nullptr;
}

void IocpWakeUpEventSource::dispatch(const void* raw_event) {
    (void)raw_event;
}

void IocpWakeUpEventSource::notify() {
    if (!backend_) return;
    backend_->postWake(key());
}

} // namespace RopHive::Windows

#endif // _WIN32

