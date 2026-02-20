#ifndef _ROP_PLATFORM_MACOS_KQUEUE_WORKER_TIMER_H
#define _ROP_PLATFORM_MACOS_KQUEUE_WORKER_TIMER_H

#ifdef __APPLE__

#include <chrono>
#include <functional>
#include <memory>

#include "../../../schedule/worker_watcher.h"

namespace RopHive::MacOS {

struct KqueueWorkerTimerState;

class KqueueWorkerTimerWatcher final : public IWorkerWatcher {
public:
    explicit KqueueWorkerTimerWatcher(IOWorker& worker, std::function<void()> callback);
    ~KqueueWorkerTimerWatcher() override;

    void start() override;
    void stop() override;

    void setSpec(std::chrono::nanoseconds initial_delay, std::chrono::nanoseconds interval);
    void clearSpec();

private:
    void createSource();

private:
    std::shared_ptr<KqueueWorkerTimerState> state_;
    bool attached_{false};
    std::shared_ptr<IEventSource> source_;
};

} // namespace RopHive::MacOS

#endif // __APPLE__

#endif // _ROP_PLATFORM_MACOS_KQUEUE_WORKER_TIMER_H
