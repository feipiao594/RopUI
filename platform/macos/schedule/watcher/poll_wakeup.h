#ifndef _ROP_PLATFORM_MACOS_EPOLL_WATCHER_POLL_WAKEUP_H
#define _ROP_PLATFORM_MACOS_EPOLL_WATCHER_POLL_WAKEUP_H

#include <memory>
#include <unistd.h>
#include "../../../schedule/eventloop.h"

namespace RopEventloop::MacOS {

class PollWakeUpWatcher final : public IWakeUpWatcher {
public:
    explicit PollWakeUpWatcher(EventLoop& loop);
    ~PollWakeUpWatcher() override;

    void start() override;
    void stop() override;
    void notify() override;

private:
    void createSource();

private:
    int pipe_fds_[2]{-1, -1};

    bool attached_{false};

    std::unique_ptr<IEventSource> source_;
};

}

#endif // _ROP_PLATFORM_MACOS_EPOLL_WATCHER_POLL_WAKEUP_H