#ifndef _ROP_PLATFORM_WINDOWS_IOCP_EVENTLOOP_BACKEND_H
#define _ROP_PLATFORM_WINDOWS_IOCP_EVENTLOOP_BACKEND_H

#ifdef _WIN32

#include <functional>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "../win32_wrapper.h"

#include "../../schedule/eventloop_core.h"

namespace RopHive::Windows {

struct IocpRawEvent {
    ULONG_PTR key;
    OVERLAPPED* overlapped;
    DWORD bytes;
    DWORD error;
};

class IocpBackend;

class IocpEventSource : public IEventSource {
public:
    explicit IocpEventSource(ULONG_PTR key, HANDLE handle = nullptr);
    ~IocpEventSource() override = default;

    void arm(IEventCoreBackend& backend) override;
    void disarm(IEventCoreBackend& backend) override;
    bool matches(const void* raw_event) const override;

protected:
    const IocpRawEvent* asIocpEvent(const void* raw_event) const;
    ULONG_PTR key() const { return key_; }
    IocpBackend* backend() const noexcept { return backend_; }

private:
    ULONG_PTR key_;
    HANDLE handle_{nullptr};
    IocpBackend* backend_{nullptr};
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

    // Associates a HANDLE with this IOCP and assigns it a completion key.
    // For sockets, pass the SOCKET cast to HANDLE.
    bool associateHandle(HANDLE handle, ULONG_PTR key);

    // Posts a completion packet into the IOCP queue (used by wakeup and testing).
    void postCompletion(ULONG_PTR key, DWORD bytes, OVERLAPPED* overlapped);

    // Convenience for "poke/wakeup" packets.
    void postWake(ULONG_PTR key) { postCompletion(key, 0, nullptr); }

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


// A convenience source that also associates a HANDLE to the IOCP on arm().
// This is the typical "attach" point for file/socket handles in an IOCP backend.
class IocpHandleCompletionEventSource final : public IocpEventSource {
public:
    using Callback = std::function<void(const IocpRawEvent&)>;

    IocpHandleCompletionEventSource(HANDLE handle, ULONG_PTR key, Callback cb);
    void dispatch(const void* raw_event) override;

private:
    Callback cb_;
};

// Special source used by wakeup watchers: it knows how to post to the same IOCP.
class IocpWakeUpEventSource final : public IocpEventSource {
public:
    explicit IocpWakeUpEventSource(ULONG_PTR key);
    void dispatch(const void* raw_event) override;
    void notify();
};

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_IOCP_EVENTLOOP_BACKEND_H
