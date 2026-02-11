#include <algorithm>

#include <log.hpp>
#include "eventloop_core.h"

namespace RopHive {

IEventLoopCore::IEventLoopCore(std::unique_ptr<IEventCoreBackend> backend)
    : backend_(std::move(backend)) {}

void IEventLoopCore::addSource(std::shared_ptr<IEventSource> source) {
    if (!source) return;
    LOG(DEBUG)("source add to core");
    std::lock_guard<std::mutex> lock(mu_);
    pending_ops_.push_back(PendingOp{PendingOpKind::Add, std::move(source)});
}

void IEventLoopCore::removeSource(std::shared_ptr<IEventSource> source) {
    if (!source) return;
    std::lock_guard<std::mutex> lock(mu_);
    pending_ops_.push_back(PendingOp{PendingOpKind::Remove, std::move(source)});
}

void IEventLoopCore::runOnce(int timeout) {
    // LOG(DEBUG)("event loop core iteration started");
    backend_->wait(timeout);
    // LOG(DEBUG)("event loop core after waited");
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
    std::vector<std::shared_ptr<IEventSource>> sources_snapshot;
    {
        std::lock_guard<std::mutex> lock(mu_);
        sources_snapshot = sources_;
    }
    // LOG(DEBUG)("span.count: %d", span.count);
    for (size_t i = 0; i < span.count; ++i) {
        const void* ev = base + i * span.stride;

        for (auto& src : sources_snapshot) {
            if (src && src->matches(ev)) {
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

    std::vector<PendingOp> ops;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ops.swap(pending_ops_);
    }

    for (auto& op : ops) {
        if (!op.src) continue;

        if (op.kind == PendingOpKind::Add) {
            backend_->addSource(op.src.get());
            op.src->arm(*backend_);

            const bool exists = std::any_of(
                sources_.begin(),
                sources_.end(),
                [&](const std::shared_ptr<IEventSource>& p) {
                    return p && p.get() == op.src.get();
                });
            if (!exists) {
                sources_.push_back(std::move(op.src));
            }
            continue;
        }

        // Remove
        op.src->disarm(*backend_);
        backend_->removeSource(op.src.get());

        auto it = std::remove_if(
            sources_.begin(),
            sources_.end(),
            [&](const std::shared_ptr<IEventSource>& p) {
                return p && p.get() == op.src.get();
            });
        sources_.erase(it, sources_.end());
    }
}

}
