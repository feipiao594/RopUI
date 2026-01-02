#ifndef _ROP_PLATFORM_ROPHIVE_WORKER_WATCHER_H
#define _ROP_PLATFORM_ROPHIVE_WORKER_WATCHER_H

namespace RopHive {

class IOWorker;
class IEventSource;

// Worker-side analog of EventLoop's IWatcher: owns a set of IEventSource instances
// and attaches/detaches them to a specific IOWorker.
class IWorkerWatcher {
public:
    virtual ~IWorkerWatcher() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

protected:
    explicit IWorkerWatcher(IOWorker& worker)
        : worker_(worker) {}

    void attachSource(IEventSource* src);
    void detachSource(IEventSource* src);

protected:
    IOWorker& worker_;
};

class IWorkerWakeUpWatcher : public IWorkerWatcher {
public:
    ~IWorkerWakeUpWatcher() override = default;
    virtual void notify() = 0;

protected:
    explicit IWorkerWakeUpWatcher(IOWorker& worker)
        : IWorkerWatcher(worker) {}
};

} // namespace RopHive

#endif // _ROP_PLATFORM_ROPHIVE_WORKER_WATCHER_H
