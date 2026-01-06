#ifndef _ROP_PLATFORM_MACOS_COCOA_WAKEUP_H
#define _ROP_PLATFORM_MACOS_COCOA_WAKEUP_H

#ifdef __APPLE__

#include "../../../schedule/eventloop.h"
#include <memory>

namespace RopHive::MacOS {

class CocoaWakeUpWatcher final : public IWakeUpWatcher {
public:
    explicit CocoaWakeUpWatcher(EventLoop& loop);
    ~CocoaWakeUpWatcher() override;

    void start() override;
    void stop() override;
    void notify() override;

private:
    std::shared_ptr<IEventSource> source_;
    bool attached_{false};
};

} // namespace RopHive::MacOS

#endif // __APPLE__

#endif // _ROP_PLATFORM_MACOS_COCOA_WAKEUP_H
