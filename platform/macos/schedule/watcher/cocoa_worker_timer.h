#ifndef _ROP_PLATFORM_MACOS_COCOA_WORKER_TIMER_H
#define _ROP_PLATFORM_MACOS_COCOA_WORKER_TIMER_H

#ifdef __APPLE__

#include <chrono>
#include <functional>
#include <memory>

#include "../../../schedule/worker_watcher.h"

namespace RopHive::MacOS {

struct CocoaWorkerTimerState;

class CocoaWorkerTimerWatcher final : public IWorkerWatcher {
public:
    explicit CocoaWorkerTimerWatcher(IOWorker& worker, std::function<void()> callback);
    ~CocoaWorkerTimerWatcher() override;

    void start() override;
    void stop() override;

    void setSpec(std::chrono::nanoseconds initial_delay, std::chrono::nanoseconds interval);
    void clearSpec();

private:
    void createSource();

private:
    std::shared_ptr<CocoaWorkerTimerState> state_;
    std::shared_ptr<IEventSource> source_;
    bool attached_{false};
};

} // namespace RopHive::MacOS

#endif // __APPLE__

#endif // _ROP_PLATFORM_MACOS_COCOA_WORKER_TIMER_H
