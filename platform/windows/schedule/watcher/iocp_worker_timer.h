#ifndef _ROP_PLATFORM_WINDOWS_IOCP_WORKER_TIMER_H
#define _ROP_PLATFORM_WINDOWS_IOCP_WORKER_TIMER_H

#ifdef _WIN32

#include <chrono>
#include <functional>
#include <memory>

#include "../../../schedule/worker_watcher.h"

namespace RopHive::Windows {

struct IocpWorkerTimerState;

class IocpWorkerTimerWatcher final : public IWorkerWatcher {
public:
    explicit IocpWorkerTimerWatcher(IOWorker& worker, std::function<void()> callback);
    ~IocpWorkerTimerWatcher() override;

    void start() override;
    void stop() override;

    void setSpec(std::chrono::nanoseconds initial_delay, std::chrono::nanoseconds interval);
    void clearSpec();

private:
    std::shared_ptr<IocpWorkerTimerState> state_;
    std::shared_ptr<IEventSource> source_;
    bool attached_{false};
};

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_IOCP_WORKER_TIMER_H
