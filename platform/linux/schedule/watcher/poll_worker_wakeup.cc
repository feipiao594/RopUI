#include "poll_worker_wakeup.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "../poll_backend.h"

namespace RopHive::Linux {

PollWorkerWakeUpWatcher::PollWorkerWakeUpWatcher(IOWorker& worker)
    : IWorkerWakeUpWatcher(worker) {
    wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeup_fd_ < 0) {
        throw std::runtime_error(
            std::string("eventfd failed: ") + std::strerror(errno));
    }
    createSource();
}

PollWorkerWakeUpWatcher::~PollWorkerWakeUpWatcher() {
    stop();
    if (wakeup_fd_ >= 0) {
        ::close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
}

void PollWorkerWakeUpWatcher::start() {
    if (attached_) return;
    attachSource(source_.get());
    attached_ = true;
}

void PollWorkerWakeUpWatcher::stop() {
    if (!attached_) return;
    detachSource(source_.get());
    attached_ = false;
}

void PollWorkerWakeUpWatcher::notify() {
    if (wakeup_fd_ < 0) return;
    uint64_t one = 1;
    const ssize_t n = ::write(wakeup_fd_, &one, sizeof(one));
    (void)n;
}

void PollWorkerWakeUpWatcher::createSource() {
    source_ = std::make_unique<PollReadinessEventSource>(
        wakeup_fd_,
        POLLIN,
        [this](uint32_t events) {
            if (!(events & POLLIN)) return;
            uint64_t value = 0;
            while (::read(wakeup_fd_, &value, sizeof(value)) > 0) {
            }
        });
}

} // namespace RopHive::Linux

