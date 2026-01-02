#include "schedule/worker_watcher.h"

#include "schedule/io_worker.h"

namespace RopHive {

void IWorkerWatcher::attachSource(IEventSource* src) {
    worker_.attachSource(src);
}

void IWorkerWatcher::detachSource(IEventSource* src) {
    worker_.detachSource(src);
}

} // namespace RopHive

