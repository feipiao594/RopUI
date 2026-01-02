#ifndef _ROP_PLATFORM_WINDOWS_IOCP_WORKER_WAKEUP_H
#define _ROP_PLATFORM_WINDOWS_IOCP_WORKER_WAKEUP_H

#ifdef _WIN32

#include <memory>

#include "../../../schedule/worker_watcher.h"

namespace RopHive::Windows {

class IocpWorkerWakeUpWatcher final : public IWorkerWakeUpWatcher {
public:
    explicit IocpWorkerWakeUpWatcher(IOWorker& worker);
    ~IocpWorkerWakeUpWatcher() override;

    void start() override;
    void stop() override;
    void notify() override;

private:
    std::unique_ptr<IEventSource> source_;
    bool attached_{false};
};

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_IOCP_WORKER_WAKEUP_H

