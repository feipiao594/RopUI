#ifndef _ROP_PLATFORM_WINDOWS_WIN32_WORKER_WAKEUP_H
#define _ROP_PLATFORM_WINDOWS_WIN32_WORKER_WAKEUP_H

#ifdef _WIN32

#include "../../../schedule/worker_watcher.h"
#include "../../win32_wrapper.h"

namespace RopHive::Windows {

class Win32WorkerWakeUpWatcher final : public IWorkerWakeUpWatcher {
public:
    explicit Win32WorkerWakeUpWatcher(IOWorker& worker);
    ~Win32WorkerWakeUpWatcher() override = default;

    void start() override;
    void stop() override;
    void notify() override;

    void setThreadId(DWORD tid) noexcept { thread_id_ = tid; }

private:
    DWORD thread_id_{0};
    bool attached_{false};
};

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_WIN32_WORKER_WAKEUP_H

