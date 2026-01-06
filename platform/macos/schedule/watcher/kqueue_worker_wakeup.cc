#include "kqueue_worker_wakeup.h"

#ifdef __APPLE__

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "../kqueue_backend.h"

namespace RopHive::MacOS {

struct KqueueWorkerWakeUpState {
    int pipe_fds_[2]{-1, -1};

    ~KqueueWorkerWakeUpState() {
        if (pipe_fds_[0] >= 0) ::close(pipe_fds_[0]);
        if (pipe_fds_[1] >= 0) ::close(pipe_fds_[1]);
    }
};

KqueueWorkerWakeUpWatcher::KqueueWorkerWakeUpWatcher(IOWorker& worker)
    : IWorkerWakeUpWatcher(worker) {
    state_ = std::make_shared<KqueueWorkerWakeUpState>();
    if (::pipe(state_->pipe_fds_) < 0) {
        throw std::runtime_error(std::string("pipe failed: ") + std::strerror(errno));
    }
    ::fcntl(state_->pipe_fds_[0], F_SETFL, O_NONBLOCK);
    ::fcntl(state_->pipe_fds_[1], F_SETFL, O_NONBLOCK);
    createSource();
}

KqueueWorkerWakeUpWatcher::~KqueueWorkerWakeUpWatcher() {
    stop();
    source_.reset();
    state_.reset();
}

void KqueueWorkerWakeUpWatcher::start() {
    if (attached_) return;
    attachSource(source_);
    attached_ = true;
}

void KqueueWorkerWakeUpWatcher::stop() {
    if (!attached_) return;
    detachSource(source_);
    attached_ = false;
}

void KqueueWorkerWakeUpWatcher::notify() {
    if (!state_ || state_->pipe_fds_[1] < 0) return;
    char one = 1;
    const ssize_t n = ::write(state_->pipe_fds_[1], &one, sizeof(one));
    (void)n;
}

void KqueueWorkerWakeUpWatcher::createSource() {
    auto state = state_;
    source_ = std::make_shared<KqueueReadinessEventSource>(
        state ? state->pipe_fds_[0] : -1,
        [state](const KqueueRawEvent&) {

            char buf[64];
            const int pipe_fds = state ? state->pipe_fds_[0] : -1;
            if (pipe_fds < 0) return;
            while (::read(pipe_fds, buf, sizeof(buf)) > 0) {
            }
        });
}

} // namespace RopHive::MacOS

#endif // __APPLE__

