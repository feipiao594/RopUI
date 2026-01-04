#include "win32_worker_wakeup.h"

#ifdef _WIN32

namespace RopHive::Windows {

static constexpr UINT kWakeMsg = WM_USER + 101;

Win32WorkerWakeUpWatcher::Win32WorkerWakeUpWatcher(IOWorker& worker)
    : IWorkerWakeUpWatcher(worker) {}

void Win32WorkerWakeUpWatcher::start() {
    thread_id_ = ::GetCurrentThreadId();
    attached_ = true;
}
void Win32WorkerWakeUpWatcher::stop() { attached_ = false; }

void Win32WorkerWakeUpWatcher::notify() {
    if (!attached_) return;
    if (thread_id_ == 0) return;
    ::PostThreadMessage(thread_id_, kWakeMsg, 0, 0);
}

} // namespace RopHive::Windows

#endif // _WIN32
