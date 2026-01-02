#include "win32_wakeup.h"

#ifdef _WIN32

#include "../win32_backend.h"

namespace RopHive::Windows {

// Use an application-defined message as a wakeup poke.
static constexpr UINT kWakeMsg = WM_USER + 100;

Win32WakeUpWatcher::Win32WakeUpWatcher(EventLoop& loop)
    : IWakeUpWatcher(loop) {
    (void)loop;
    thread_id_ = ::GetCurrentThreadId();
}

void Win32WakeUpWatcher::start() { attached_ = true; }
void Win32WakeUpWatcher::stop() { attached_ = false; }

void Win32WakeUpWatcher::notify() {
    if (!attached_) return;
    ::PostThreadMessage(thread_id_, kWakeMsg, 0, 0);
}

} // namespace RopHive::Windows

#endif // _WIN32

