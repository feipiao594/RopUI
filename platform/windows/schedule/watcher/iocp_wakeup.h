#ifndef _ROP_PLATFORM_WINDOWS_IOCP_WAKEUP_H
#define _ROP_PLATFORM_WINDOWS_IOCP_WAKEUP_H

#ifdef _WIN32

#include <memory>

#include "../../../schedule/eventloop.h"

namespace RopHive::Windows {

class IocpWakeUpWatcher final : public IWakeUpWatcher {
public:
    explicit IocpWakeUpWatcher(EventLoop& loop);
    ~IocpWakeUpWatcher() override;

    void start() override;
    void stop() override;
    void notify() override;

private:
    std::unique_ptr<IEventSource> source_;
    bool attached_{false};
};

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_IOCP_WAKEUP_H

