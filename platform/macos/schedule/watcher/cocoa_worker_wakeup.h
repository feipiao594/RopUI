#ifndef _ROP_PLATFORM_MACOS_COCOA_WORKER_WAKEUP_H
#define _ROP_PLATFORM_MACOS_COCOA_WORKER_WAKEUP_H

#ifdef __APPLE__

#include "../../../schedule/worker_watcher.h"

#include <memory>

namespace RopHive::MacOS {

class CocoaWorkerWakeUpWatcher final : public IWorkerWakeUpWatcher {
public:
    explicit CocoaWorkerWakeUpWatcher(IOWorker& worker);
    ~CocoaWorkerWakeUpWatcher() override;

    void start() override;
    void stop() override;
    void notify() override;

private:
    std::unique_ptr<IEventSource> source_;
    bool attached_{false};
};

} // namespace RopHive::MacOS

#endif // __APPLE__

#endif // _ROP_PLATFORM_MACOS_COCOA_WORKER_WAKEUP_H
