#ifndef _ROP_PLATFORM_LINUX_POLL_WORKER_WAKEUP_H
#define _ROP_PLATFORM_LINUX_POLL_WORKER_WAKEUP_H

#include <memory>

#include "../../../schedule/worker_watcher.h"

namespace RopHive::Linux {

class PollWorkerWakeUpWatcher final : public IWorkerWakeUpWatcher {
public:
    explicit PollWorkerWakeUpWatcher(IOWorker& worker);
    ~PollWorkerWakeUpWatcher() override;

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

#endif // _ROP_PLATFORM_LINUX_POLL_WORKER_WAKEUP_H

