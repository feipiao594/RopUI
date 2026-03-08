#ifndef _ROP_PLATFORM_WINDOWS_WIN32_WORKER_TIMER_H
#define _ROP_PLATFORM_WINDOWS_WIN32_WORKER_TIMER_H

#ifdef _WIN32

#include <chrono>
#include <functional>
#include <memory>

#include "../../../schedule/worker_watcher.h"
#include "../../win32_wrapper.h"

namespace RopHive::Windows {

struct Win32WorkerTimerState;

class Win32WorkerTimerWatcher final : public IWorkerWatcher {
public:
    explicit Win32WorkerTimerWatcher(IOWorker& worker, std::function<void()> callback);
    ~Win32WorkerTimerWatcher() override;

    void start() override;
    void stop() override;

    void setSpec(std::chrono::nanoseconds initial_delay, std::chrono::nanoseconds interval);
    void clearSpec();

private:
    std::shared_ptr<Win32WorkerTimerState> state_;
    std::shared_ptr<IEventSource> source_;
    bool attached_{false};
};

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_WIN32_WORKER_TIMER_H
