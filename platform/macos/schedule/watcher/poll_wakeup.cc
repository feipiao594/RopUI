#include "poll_wakeup.h"

#include <memory>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "../poll_backend.h"

namespace RopHive::MacOS {

struct PollWakeUpState {
    int pipe_fds_[2]{-1, -1};
    
    ~PollWakeUpState() {
        if (pipe_fds_[0] >= 0) ::close(pipe_fds_[0]);
        if (pipe_fds_[1] >= 0) ::close(pipe_fds_[1]);
    }
};

PollWakeUpWatcher::PollWakeUpWatcher(EventLoop& loop)
    : IWakeUpWatcher(loop) {
    state_ = std::make_shared<PollWakeUpState>();
    if (::pipe(state_->pipe_fds_) < 0) {
        throw std::runtime_error(
            std::string("pipe failed: ") + std::strerror(errno));
    }

    ::fcntl(state_->pipe_fds_[0], F_SETFL, O_NONBLOCK);
    ::fcntl(state_->pipe_fds_[1], F_SETFL, O_NONBLOCK);

    createSource();
}

PollWakeUpWatcher::~PollWakeUpWatcher() {
    stop();

    source_.reset();
    state_.reset(); 
}

void PollWakeUpWatcher::start() {
    if (attached_) return;
    attachSource(source_);
    attached_ = true;
}

void PollWakeUpWatcher::stop() {
    if (!attached_) return;
    detachSource(source_);
    attached_ = false;
}

void PollWakeUpWatcher::notify() {
    if (!state_ || state_->pipe_fds_[1] < 0) return;

    char one = 1;
    ssize_t n = ::write(state_->pipe_fds_[1], &one, sizeof(one));
    (void)n;
}

void PollWakeUpWatcher::createSource() {
    auto state = state_;
    source_ = std::make_shared<PollReadinessEventSource>(
        state ? state->pipe_fds_[0] : -1,
        POLLIN,
        [state](uint32_t events) {
            if (!(events & POLLIN)) return;

            uint64_t value = 0;
            const int pipe_fds = state ? state->pipe_fds_[0] : -1;
            if (pipe_fds < 0) return;
            while (::read(state->pipe_fds_[0], &value, sizeof(value)) > 0) {
                // read until EAGAIN
            }
        });
}

}