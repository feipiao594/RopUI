#ifndef _ROP_PLATFORM_MACOS_KQUEUE_WAKEUP_H
#define _ROP_PLATFORM_MACOS_KQUEUE_WAKEUP_H

#ifdef __APPLE__

#include <memory>

#include "../../../schedule/eventloop.h"

namespace RopHive::MacOS {

struct KqueueWakeUpState;

class KqueueWakeUpWatcher final : public IWakeUpWatcher {
public:
    explicit KqueueWakeUpWatcher(EventLoop& loop);
    ~KqueueWakeUpWatcher() override;

    void start() override;
    void stop() override;
    void notify() override;

private:
    void createSource();

private:
    std::shared_ptr<KqueueWakeUpState> state_;
    bool attached_{false};
    std::shared_ptr<IEventSource> source_;
};

} // namespace RopHive::MacOS

#endif // __APPLE__

#endif // _ROP_PLATFORM_MACOS_KQUEUE_WAKEUP_H

