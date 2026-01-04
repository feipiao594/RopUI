#ifndef _ROP_PLATFORM_WINDOWS_WIN32_EVENTLOOP_BACKEND_H
#define _ROP_PLATFORM_WINDOWS_WIN32_EVENTLOOP_BACKEND_H

#ifdef _WIN32

#include <functional>
#include <unordered_map>
#include <vector>

#include "../win32_wrapper.h"

#include "../../schedule/eventloop_core.h"

namespace RopHive::Windows {

enum class Win32RawEventKind : uint8_t { // for padding
    Message,
    Handle
};

struct Win32RawEvent {
    Win32RawEventKind kind;

    // kind == Message
    UINT msg;
    WPARAM wparam;
    LPARAM lparam;

    // kind == Handle
    HANDLE handle;
};

class Win32MessageSource : public IEventSource {
public:
    explicit Win32MessageSource(UINT msg);
    ~Win32MessageSource() override = default;

    void arm(IEventCoreBackend& backend) override;
    void disarm(IEventCoreBackend& backend) override;

    bool matches(const void* raw_event) const override;

protected:
    const Win32RawEvent* asEvent(const void* raw_event) const;

private:
    UINT msg_;
    bool armed_ = false;
};

class Win32HandleSource : public IEventSource {
public:
    explicit Win32HandleSource(HANDLE handle);
    ~Win32HandleSource() override = default;

    void arm(IEventCoreBackend& backend) override;
    void disarm(IEventCoreBackend& backend) override;

    bool matches(const void* raw_event) const override;

protected:
    const Win32RawEvent* asEvent(const void* raw_event) const;
    HANDLE handle() const { return handle_; }

private:
    HANDLE handle_{nullptr};
    bool armed_ = false;
};

class Win32Backend final : public IEventCoreBackend {
public:
    Win32Backend();
    ~Win32Backend() override = default;

    void addSource(IEventSource* source) override;
    void removeSource(IEventSource* source) override;

    void wait(int timeout) override;
    RawEventSpan rawEvents() const override;

    DWORD threadId() const noexcept { return thread_id_; }

    void registerHandle(HANDLE h);
    void unregisterHandle(HANDLE h);

private:
    DWORD thread_id_;
    std::vector<HANDLE> handles_;
    std::unordered_map<HANDLE, size_t> handle_index_;

    std::vector<Win32RawEvent> ready_;
};

class Win32EventLoopCore final : public IEventLoopCore {
public:
    Win32EventLoopCore();
    ~Win32EventLoopCore() override = default;
};

class Win32MessageEventSource final : public Win32MessageSource {
public:
    using Callback = std::function<void(const Win32RawEvent&)>;

    Win32MessageEventSource(UINT msg, Callback cb);
    void dispatch(const void* raw_event) override;

private:
    Callback cb_;
};

class Win32HandleEventSource final : public Win32HandleSource {
public:
    using Callback = std::function<void(HANDLE)>;

    Win32HandleEventSource(HANDLE handle, Callback cb);
    void dispatch(const void* raw_event) override;

private:
    Callback cb_;
};

} // namespace RopHive::Windows

#endif // _WIN32

#endif // _ROP_PLATFORM_WINDOWS_WIN32_EVENTLOOP_BACKEND_H

