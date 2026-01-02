#include "epoll_worker_wakeup.h"

#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "../epoll_backend.h"

namespace RopHive::Linux {

EpollWorkerWakeUpWatcher::EpollWorkerWakeUpWatcher(IOWorker& worker)
    : IWorkerWakeUpWatcher(worker) {
    wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeup_fd_ < 0) {
        throw std::runtime_error(
            std::string("eventfd failed: ") + std::strerror(errno));
    }
    createSource();
}

EpollWorkerWakeUpWatcher::~EpollWorkerWakeUpWatcher() {
    stop();
    if (wakeup_fd_ >= 0) {
        ::close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
}

void EpollWorkerWakeUpWatcher::start() {
    if (attached_) return;
    attachSource(source_.get());
    attached_ = true;
}

void EpollWorkerWakeUpWatcher::stop() {
    if (!attached_) return;
    detachSource(source_.get());
    attached_ = false;
}

void EpollWorkerWakeUpWatcher::notify() {
    if (wakeup_fd_ < 0) return;
    uint64_t one = 1;
    const ssize_t n = ::write(wakeup_fd_, &one, sizeof(one));
    (void)n;
}

void EpollWorkerWakeUpWatcher::createSource() {
    source_ = std::make_unique<EpollReadinessEventSource>(
        wakeup_fd_,
        EPOLLIN,
        [this](uint32_t events) {
            if (!(events & EPOLLIN)) return;
            uint64_t value = 0;
            while (::read(wakeup_fd_, &value, sizeof(value)) > 0) {
            }
        });
}

} // namespace RopHive::Linux

