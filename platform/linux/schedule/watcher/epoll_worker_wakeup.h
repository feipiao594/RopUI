#ifndef _ROP_PLATFORM_LINUX_EPOLL_WORKER_WAKEUP_H
#define _ROP_PLATFORM_LINUX_EPOLL_WORKER_WAKEUP_H

#include <memory>

#include "../../../schedule/worker_watcher.h"

namespace RopHive::Linux {

class EpollWorkerWakeUpWatcher final : public IWorkerWakeUpWatcher {
public:
    explicit EpollWorkerWakeUpWatcher(IOWorker& worker);
    ~EpollWorkerWakeUpWatcher() override;

    void start() override;
    void stop() override;
    void notify() override;

private:
    void createSource();

private:
    int wakeup_fd_{-1};
    bool attached_{false};
    std::unique_ptr<IEventSource> source_;
};

} // namespace RopHive::Linux

#endif // _ROP_PLATFORM_LINUX_EPOLL_WORKER_WAKEUP_H

