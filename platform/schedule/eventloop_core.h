#ifndef _ROP_PLATFORM_EVENTLOOP_CORE_H
#define _ROP_PLATFORM_EVENTLOOP_CORE_H

#include <memory>
#include <vector>
#include <cstddef>

namespace RopEventloop {

enum class BackendType {
    LINUX_POLL,
    LINUX_EPOLL,
    MACOS_POLL,
    MACOS_KQUEUE
};

struct RawEventSpan {
    const void* data;
    size_t count;
    size_t stride;
};

class IEventSource;
class IEventCoreBackend;

class IEventCoreBackend {
    BackendType type_;
public:
    IEventCoreBackend(BackendType type) : type_(type) {}
    virtual ~IEventCoreBackend() = default;

    virtual void addSource(IEventSource* source) = 0;
    virtual void removeSource(IEventSource* source) = 0;

    virtual void wait(int timeout) = 0;

    virtual RawEventSpan rawEvents() const = 0;

    BackendType getType() {
        return type_;
    }
};

class IEventSource {
protected:
    BackendType type_;
public:
    IEventSource(BackendType type) : type_(type) {};
    virtual ~IEventSource() = default;
    
    virtual void arm(IEventCoreBackend& backend) = 0;
    virtual void disarm(IEventCoreBackend& backend) = 0;

    virtual bool matches(const void* raw_event) const = 0;
    virtual void dispatch(const void* raw_event) = 0;

    bool isSourceMatchBackend(IEventCoreBackend* backend) {
        return type_ == backend->getType();
    }
};

class IEventLoopCore {
public:
    virtual ~IEventLoopCore() = default;

    void applyInitialChanges() {
        applyPendingChanges();
    }
    void runOnce(int timeout = -1);
    
    void addSource(IEventSource* source);
    void removeSource(IEventSource* source);
    
protected:
    explicit IEventLoopCore(std::unique_ptr<IEventCoreBackend> backend);
    virtual void dispatchRawEvents();
    
private:
    void applyPendingChanges();

private:
    std::unique_ptr<IEventCoreBackend> backend_;

    std::vector<IEventSource*> sources_;
    std::vector<IEventSource*> pending_add_;
    std::vector<IEventSource*> pending_remove_;

    bool in_dispatch_ = false;
};

}

#endif //_ROP_PLATFORM_EVENTLOOP_CORE_H