#ifndef _ROP_PLATFORM_MACOS_POLL_WORKER_TIMER_H
#define _ROP_PLATFORM_MACOS_POLL_WORKER_TIMER_H

#ifdef __APPLE__

#include <chrono>
#include <functional>
#include <memory>

#include "../../../schedule/worker_watcher.h"

namespace RopHive::MacOS {

struct PollWorkerTimerState;

class PollWorkerTimerWatcher final : public IWorkerWatcher {
public:
    explicit PollWorkerTimerWatcher(IOWorker& worker, std::function<void()> callback);
    ~PollWorkerTimerWatcher() override;

    void start() override;
    void stop() override;

    void setSpec(std::chrono::nanoseconds initial_delay, std::chrono::nanoseconds interval);
    void clearSpec();

private:
    void createSource();

private:
    std::shared_ptr<PollWorkerTimerState> state_;
    bool attached_{false};
    std::shared_ptr<IEventSource> source_;
};

} // namespace RopHive::MacOS

#endif // __APPLE__

#endif // _ROP_PLATFORM_MACOS_POLL_WORKER_TIMER_H
