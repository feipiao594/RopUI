#include <type_traits>

#include "iocp_backend.h"

#ifdef _WIN32

namespace RopHive::Windows {

IocpEventSource::IocpEventSource(ULONG_PTR key, HANDLE handle)
    : IEventSource(BackendType::WINDOWS_IOCP),
      key_(key),
      handle_(handle) {}

void IocpEventSource::arm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        armed_ = false;
        backend_ = nullptr;
        return;
    }
    armed_ = true;
    backend_ = static_cast<IocpBackend*>(&backend);

    if (handle_ != nullptr) {
        backend_->associateHandle(handle_, key());
    }
}

void IocpEventSource::disarm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    armed_ = false;
    backend_ = nullptr;
    (void)backend;
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

bool IocpBackend::associateHandle(HANDLE handle, ULONG_PTR key) {
    if (!port_ || handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    HANDLE r = ::CreateIoCompletionPort(handle, port_, key, 0);
    return r == port_;
}

void IocpBackend::postCompletion(ULONG_PTR key, DWORD bytes, OVERLAPPED* overlapped) {
    if (!port_) return;
    ::PostQueuedCompletionStatus(port_, bytes, key, overlapped);
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

IocpHandleCompletionEventSource::IocpHandleCompletionEventSource(HANDLE handle, ULONG_PTR key, Callback cb)
    : IocpEventSource(key, handle),
      cb_(std::move(cb)) {}

void IocpHandleCompletionEventSource::dispatch(const void* raw_event) {
    if (!cb_) return;
    cb_(*asIocpEvent(raw_event));
}

IocpWakeUpEventSource::IocpWakeUpEventSource(ULONG_PTR key)
    : IocpEventSource(key) {}

void IocpWakeUpEventSource::dispatch(const void* raw_event) {
    (void)raw_event;
}

void IocpWakeUpEventSource::notify() {
    auto* b = backend();
    if (!b) return;
    b->postWake(key());
}

} // namespace RopHive::Windows

#endif // _WIN32

