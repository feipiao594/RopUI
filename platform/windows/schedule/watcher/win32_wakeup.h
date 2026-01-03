#ifndef _ROP_PLATFORM_WINDOWS_WIN32_WAKEUP_H
#define _ROP_PLATFORM_WINDOWS_WIN32_WAKEUP_H

#ifdef _WIN32

#include "../../../schedule/eventloop.h"
#include "../../win32_wrapper.h"

namespace RopHive::Windows {

class Win32WakeUpWatcher final : public IWakeUpWatcher {
public:
    explicit Win32WakeUpWatcher(EventLoop& loop);
    ~Win32WakeUpWatcher() override = default;

    void start() override;
    void stop() override;
    void notify() override;

private:
    DWORD thread_id_{0};
    bool attached_{false};
};

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_WIN32_WAKEUP_H

