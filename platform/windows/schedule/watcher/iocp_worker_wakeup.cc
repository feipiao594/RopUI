#include "iocp_worker_wakeup.h"

#ifdef _WIN32

#include "../iocp_backend.h"

namespace RopHive::Windows {

static constexpr ULONG_PTR kWakeKey = 0xC0DEC0DF;

IocpWorkerWakeUpWatcher::IocpWorkerWakeUpWatcher(IOWorker& worker)
    : IWorkerWakeUpWatcher(worker) {
    source_ = std::make_unique<IocpWakeUpEventSource>(kWakeKey);
}

IocpWorkerWakeUpWatcher::~IocpWorkerWakeUpWatcher() {
    stop();
}

void IocpWorkerWakeUpWatcher::start() {
    if (attached_) return;
    attachSource(source_.get());
    attached_ = true;
}

void IocpWorkerWakeUpWatcher::stop() {
    if (!attached_) return;
    detachSource(source_.get());
    attached_ = false;
}

void IocpWorkerWakeUpWatcher::notify() {
    if (!attached_) return;
    static_cast<IocpWakeUpEventSource*>(source_.get())->notify();
}

} // namespace RopHive::Windows

#endif // _WIN32

