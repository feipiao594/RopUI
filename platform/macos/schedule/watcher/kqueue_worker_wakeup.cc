#include "kqueue_worker_wakeup.h"

#ifdef __APPLE__

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "../kqueue_backend.h"

namespace RopHive::MacOS {

KqueueWorkerWakeUpWatcher::KqueueWorkerWakeUpWatcher(IOWorker& worker)
    : IWorkerWakeUpWatcher(worker) {
    if (::pipe(pipe_fds_) < 0) {
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    }
    ::fcntl(pipe_fds_[0], F_SETFL, O_NONBLOCK);
    ::fcntl(pipe_fds_[1], F_SETFL, O_NONBLOCK);
    createSource();
}

KqueueWorkerWakeUpWatcher::~KqueueWorkerWakeUpWatcher() {
    stop();
    if (pipe_fds_[0] >= 0) ::close(pipe_fds_[0]);
    if (pipe_fds_[1] >= 0) ::close(pipe_fds_[1]);
}

void KqueueWorkerWakeUpWatcher::start() {
    if (attached_) return;
    attachSource(source_.get());
    attached_ = true;
}

void KqueueWorkerWakeUpWatcher::stop() {
    if (!attached_) return;
    detachSource(source_.get());
    attached_ = false;
}

void KqueueWorkerWakeUpWatcher::notify() {
    if (pipe_fds_[1] < 0) return;
    char one = 1;
    const ssize_t n = ::write(pipe_fds_[1], &one, sizeof(one));
    (void)n;
}

void KqueueWorkerWakeUpWatcher::createSource() {
    source_ = std::make_unique<KqueueReadinessEventSource>(
        pipe_fds_[0],
        [this](const KqueueRawEvent&) {
            char buf[64];
            while (::read(pipe_fds_[0], buf, sizeof(buf)) > 0) {
            }
        });
}

} // namespace RopHive::MacOS

#endif // __APPLE__

