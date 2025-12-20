#include <algorithm>

#include <log.hpp>
#include "eventloop_core.h"

namespace RopEventloop {

IEventLoopCore::IEventLoopCore(std::unique_ptr<IEventCoreBackend> backend)
    : backend_(std::move(backend)) {}

void IEventLoopCore::addSource(std::unique_ptr<IEventSource> source) {
    IEventSource* raw = source.get();
    sources_.push_back(std::move(source));
    pending_add_.push_back(raw);
}

void IEventLoopCore::removeSource(IEventSource* source) {
    pending_remove_.push_back(source);
}

void IEventLoopCore::runOnce(int timeout) {
    LOG(DEBUG)("event loop iteration started");
    backend_->wait(timeout);
    dispatchRawEvents();
    applyPendingChanges();
}

void IEventLoopCore::dispatchRawEvents() {
    if (in_dispatch_) {
        return;
    }

    in_dispatch_ = true;

    RawEventSpan span = backend_->rawEvents();
    const char* base = static_cast<const char*>(span.data);

    for (size_t i = 0; i < span.count; ++i) {
        const void* ev = base + i * span.stride;

        for (auto& src : sources_) {
            if (src->matches(ev)) {
                src->dispatch(ev);
                break;
            }
        }
    }

    in_dispatch_ = false;
}

void IEventLoopCore::applyPendingChanges() {
    if (in_dispatch_) {
        return;
    }

    for (IEventSource* src : pending_add_) {
        backend_->addSource(src);
        src->arm(*backend_);
    }
    pending_add_.clear();

    for (IEventSource* src : pending_remove_) {
        src->disarm(*backend_);
        backend_->removeSource(src);

        auto it = std::remove_if(
            sources_.begin(),
            sources_.end(),
            [src](const std::unique_ptr<IEventSource>& p) {
                return p.get() == src;
            });

        sources_.erase(it, sources_.end());
    }
    pending_remove_.clear();
}

}