#include "win32_backend.h"

#ifdef _WIN32

#include <algorithm>

namespace RopHive::Windows {

Win32MessageSource::Win32MessageSource(UINT msg)
    : IEventSource(BackendType::WINDOWS_WIN32), msg_(msg) {}

void Win32MessageSource::arm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    armed_ = true;
}

void Win32MessageSource::disarm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    armed_ = false;
}

bool Win32MessageSource::matches(const void* raw_event) const {
    if (!armed_) return false;
    const auto* ev = static_cast<const Win32RawEvent*>(raw_event);
    return ev->kind == Win32RawEventKind::Message && ev->msg == msg_;
}

const Win32RawEvent* Win32MessageSource::asEvent(const void* raw_event) const {
    return static_cast<const Win32RawEvent*>(raw_event);
}

Win32HandleSource::Win32HandleSource(HANDLE handle)
    : IEventSource(BackendType::WINDOWS_WIN32), handle_(handle) {}

void Win32HandleSource::arm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    if (armed_) return;
    static_cast<Win32Backend&>(backend).registerHandle(handle_);
    armed_ = true;
}

void Win32HandleSource::disarm(IEventCoreBackend& backend) {
    if (!isSourceMatchBackend(&backend)) {
        return;
    }
    if (!armed_) return;
    static_cast<Win32Backend&>(backend).unregisterHandle(handle_);
    armed_ = false;
}

bool Win32HandleSource::matches(const void* raw_event) const {
    if (!armed_) return false;
    const auto* ev = static_cast<const Win32RawEvent*>(raw_event);
    return ev->kind == Win32RawEventKind::Handle && ev->handle == handle_;
}

const Win32RawEvent* Win32HandleSource::asEvent(const void* raw_event) const {
    return static_cast<const Win32RawEvent*>(raw_event);
}

Win32Backend::Win32Backend()
    : IEventCoreBackend(BackendType::WINDOWS_WIN32),
      thread_id_(::GetCurrentThreadId()) {
    // Ensure message queue exists.
    MSG msg;
    ::PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);
}

void Win32Backend::addSource(IEventSource*) {}
void Win32Backend::removeSource(IEventSource*) {}

void Win32Backend::registerHandle(HANDLE h) {
    if (h == nullptr) return;
    if (handle_index_.find(h) != handle_index_.end()) {
        return;
    }
    const size_t idx = handles_.size();
    handles_.push_back(h);
    handle_index_[h] = idx;
}

void Win32Backend::unregisterHandle(HANDLE h) {
    auto it = handle_index_.find(h);
    if (it == handle_index_.end()) {
        return;
    }
    const size_t idx = it->second;
    const size_t last = handles_.size() - 1;
    if (idx != last) {
        HANDLE moved = handles_[last];
        handles_[idx] = moved;
        handle_index_[moved] = idx;
    }
    handles_.pop_back();
    handle_index_.erase(it);
}

void Win32Backend::wait(int timeout) {
    ready_.clear();

    DWORD ms = (timeout < 0) ? INFINITE : static_cast<DWORD>(timeout);

    const DWORD handle_count = static_cast<DWORD>(handles_.size());
    const DWORD r = ::MsgWaitForMultipleObjectsEx(
        handle_count,
        handle_count ? handles_.data() : nullptr,
        ms,
        QS_ALLINPUT,
        MWMO_INPUTAVAILABLE);

    if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + handle_count) {
        const DWORD idx = r - WAIT_OBJECT_0;
        ready_.push_back(Win32RawEvent{
            .kind = Win32RawEventKind::Handle,
            .msg = 0,
            .wparam = 0,
            .lparam = 0,
            .handle = handles_[idx],
        });
    }

    // Drain messages and convert them to "raw events" for dispatch.
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ready_.push_back(Win32RawEvent{
            .kind = Win32RawEventKind::Message,
            .msg = msg.message,
            .wparam = msg.wParam,
            .lparam = msg.lParam,
            .handle = nullptr,
        });
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }
}

RawEventSpan Win32Backend::rawEvents() const {
    return RawEventSpan{
        .data = ready_.data(),
        .count = ready_.size(),
        .stride = sizeof(Win32RawEvent),
    };
}

Win32EventLoopCore::Win32EventLoopCore()
    : IEventLoopCore(std::make_unique<Win32Backend>()) {}

Win32MessageEventSource::Win32MessageEventSource(UINT msg, Callback cb)
    : Win32MessageSource(msg),
      cb_(std::move(cb)) {}

void Win32MessageEventSource::dispatch(const void* raw_event) {
    if (!cb_) return;
    cb_(*asEvent(raw_event));
}

Win32HandleEventSource::Win32HandleEventSource(HANDLE handle, Callback cb)
    : Win32HandleSource(handle),
      cb_(std::move(cb)) {}

void Win32HandleEventSource::dispatch(const void* raw_event) {
    if (!cb_) return;
    const auto* ev = asEvent(raw_event);
    cb_(ev->handle);
}

} // namespace RopHive::Windows

#endif // _WIN32
