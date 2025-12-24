#include "poll_wakeup.h"

#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "../poll_backend.h"

namespace RopEventloop::MacOS {

PollWakeUpWatcher::PollWakeUpWatcher(EventLoop& loop)
    : IWakeUpWatcher(loop) {

    if (::pipe(pipe_fds_) < 0) {
        throw std::runtime_error(
            std::string("pipe failed: ") + std::strerror(errno));
    }

    ::fcntl(pipe_fds_[0], F_SETFL, O_NONBLOCK);
    ::fcntl(pipe_fds_[1], F_SETFL, O_NONBLOCK);

    createSource();
}

PollWakeUpWatcher::~PollWakeUpWatcher() {
    stop();

    if (pipe_fds_[0] >= 0) ::close(pipe_fds_[0]);
    if (pipe_fds_[1] >= 0) ::close(pipe_fds_[1]);
}

void PollWakeUpWatcher::start() {
    if (attached_) return;
    attachSource(source_.get());
    attached_ = true;
}

void PollWakeUpWatcher::stop() {
    if (!attached_) return;
    detachSource(source_.get());
    attached_ = false;
}

void PollWakeUpWatcher::notify() {
    if (pipe_fds_[1] < 0) return;

    char one = 1;
    ssize_t n = ::write(pipe_fds_[1], &one, sizeof(one));
    (void)n;
}

void PollWakeUpWatcher::createSource() {
    source_ = std::make_unique<PollReadinessEventSource>(
        pipe_fds_[0],
        POLLIN,
        [this](uint32_t events) {
            if (!(events & POLLIN)) return;

            uint64_t value;
            while (::read(pipe_fds_[0], &value, sizeof(value)) > 0) {
                // read until EAGAIN
            }
        });
}

}