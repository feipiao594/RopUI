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
    const auto* ev = static_cast<const Win32RawMessage*>(raw_event);
    return ev->msg == msg_;
}

const Win32RawMessage* Win32MessageSource::asMsg(const void* raw_event) const {
    return static_cast<const Win32RawMessage*>(raw_event);
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

void Win32Backend::wait(int timeout) {
    ready_.clear();

    DWORD ms = (timeout < 0) ? INFINITE : static_cast<DWORD>(timeout);

    // Wait until there is input in the message queue.
    ::MsgWaitForMultipleObjectsEx(
        0, nullptr, ms,
        QS_ALLINPUT,
        MWMO_INPUTAVAILABLE);

    // Drain messages and convert them to "raw events" for dispatch.
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        ready_.push_back(Win32RawMessage{msg.message, msg.wParam, msg.lParam});
        ::TranslateMessage(&msg);
        ::DispatchMessage(&msg);
    }
}

RawEventSpan Win32Backend::rawEvents() const {
    return RawEventSpan{
        .data = ready_.data(),
        .count = ready_.size(),
        .stride = sizeof(Win32RawMessage),
    };
}

Win32EventLoopCore::Win32EventLoopCore()
    : IEventLoopCore(std::make_unique<Win32Backend>()) {}

Win32MessageEventSource::Win32MessageEventSource(UINT msg, Callback cb)
    : Win32MessageSource(msg),
      cb_(std::move(cb)) {}

void Win32MessageEventSource::dispatch(const void* raw_event) {
    if (!cb_) return;
    cb_(*asMsg(raw_event));
}

} // namespace RopHive::Windows

#endif // _WIN32

