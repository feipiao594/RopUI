#ifndef _ROP_PLATFORM_EVENTLOOP_H
#define _ROP_PLATFORM_EVENTLOOP_H

#include <memory>
#include <vector>
#include <functional>
#include <cstddef>

namespace RopEventloop {

struct RawEventSpan {
    const void* data;
    size_t count;
    size_t stride;
};

class IEventSource;
class IEventCoreBackend;

class IEventCoreBackend {
public:
    virtual ~IEventCoreBackend() = default;

    virtual void addSource(IEventSource* source) = 0;
    virtual void removeSource(IEventSource* source) = 0;

    virtual void wait() = 0;

    virtual RawEventSpan rawEvents() const = 0;
};

class IEventSource {
public:
    virtual ~IEventSource() = default;

    virtual void arm(IEventCoreBackend& backend) = 0;
    virtual void disarm(IEventCoreBackend& backend) = 0;

    virtual bool matches(const void* raw_event) const = 0;
    virtual void dispatch(const void* raw_event) = 0;
};

class IEventLoopCore {
public:
    virtual ~IEventLoopCore() = default;

    void run();

    void requestExit();

    void addSource(std::unique_ptr<IEventSource> source);
    void removeSource(IEventSource* source);

    void setLoopBegin(std::function<void()> fn);
    void setLoopEnd(std::function<void()> fn);
    void unsetLoopBegin();
    void unsetLoopEnd();

protected:
    explicit IEventLoopCore(std::unique_ptr<IEventCoreBackend> backend);

    virtual bool shouldExit() const;

private:
    void applyPendingChanges();
    void dispatchRawEvents();

private:
    std::unique_ptr<IEventCoreBackend> backend_;

    std::vector<std::unique_ptr<IEventSource>> sources_;
    std::vector<IEventSource*> pending_add_;
    std::vector<IEventSource*> pending_remove_;

    std::function<void()> on_loop_begin_;
    std::function<void()> on_loop_end_;

    bool exit_requested_ = false;
    bool in_dispatch_ = false;
};

}

#endif //_ROP_PLATFORM_EVENTLOOP_H