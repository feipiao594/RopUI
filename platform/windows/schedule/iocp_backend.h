#ifndef _ROP_PLATFORM_WINDOWS_IOCP_EVENTLOOP_BACKEND_H
#define _ROP_PLATFORM_WINDOWS_IOCP_EVENTLOOP_BACKEND_H

#ifdef _WIN32

#include <functional>
#include <unordered_map>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "../../schedule/eventloop_core.h"

namespace RopHive::Windows {

struct IocpRawEvent {
    ULONG_PTR key;
    OVERLAPPED* overlapped;
    DWORD bytes;
    DWORD error;
};

class IocpEventSource : public IEventSource {
public:
    explicit IocpEventSource(ULONG_PTR key);
    ~IocpEventSource() override = default;

    void arm(IEventCoreBackend& backend) override;
    void disarm(IEventCoreBackend& backend) override;
    bool matches(const void* raw_event) const override;

protected:
    const IocpRawEvent* asIocpEvent(const void* raw_event) const;
    ULONG_PTR key() const { return key_; }

private:
    ULONG_PTR key_;
    bool armed_ = false;
};

class IocpBackend final : public IEventCoreBackend {
public:
    IocpBackend(size_t max_events = 64);
    ~IocpBackend() override;

    void addSource(IEventSource* source) override;
    void removeSource(IEventSource* source) override;

    void wait(int timeout) override;
    RawEventSpan rawEvents() const override;

    HANDLE port() const noexcept { return port_; }
    void postWake(ULONG_PTR key);

private:
    HANDLE port_{nullptr};
    size_t max_events_;
    std::vector<IocpRawEvent> ready_;
};

class IocpEventLoopCore final : public IEventLoopCore {
public:
    IocpEventLoopCore();
    ~IocpEventLoopCore() override = default;
};

class IocpCompletionEventSource final : public IocpEventSource {
public:
    using Callback = std::function<void(const IocpRawEvent&)>;

    IocpCompletionEventSource(ULONG_PTR key, Callback cb);
    void dispatch(const void* raw_event) override;

private:
    Callback cb_;
    IocpBackend* backend_{nullptr};
};

// Special source used by wakeup watchers: it knows how to post to the same IOCP.
class IocpWakeUpEventSource final : public IocpEventSource {
public:
    explicit IocpWakeUpEventSource(ULONG_PTR key);
    void dispatch(const void* raw_event) override;
    void notify();

    void arm(IEventCoreBackend& backend) override;
    void disarm(IEventCoreBackend& backend) override;

private:
    IocpBackend* backend_{nullptr};
};

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_IOCP_EVENTLOOP_BACKEND_H

