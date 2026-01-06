#ifndef _ROP_PLATFORM_MACOS_KQUEUE_WORKER_WAKEUP_H
#define _ROP_PLATFORM_MACOS_KQUEUE_WORKER_WAKEUP_H

#ifdef __APPLE__

#include <memory>

#include "../../../schedule/worker_watcher.h"

namespace RopHive::MacOS {

struct KqueueWorkerWakeUpState;
class KqueueWorkerWakeUpWatcher final : public IWorkerWakeUpWatcher {
public:
    explicit KqueueWorkerWakeUpWatcher(IOWorker& worker);
    ~KqueueWorkerWakeUpWatcher() override;

    void start() override;
    void stop() override;
    void notify() override;

private:
    void createSource();

private:
    std::shared_ptr<KqueueWorkerWakeUpState> state_;
    bool attached_{false};
    std::shared_ptr<IEventSource> source_;
};

} // namespace RopHive::MacOS

#endif // __APPLE__

#endif // _ROP_PLATFORM_MACOS_KQUEUE_WORKER_WAKEUP_H

