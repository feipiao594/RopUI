#ifndef _ROP_PLATFORM_ROPHIVE_COMPUTE_WORKER_H
#define _ROP_PLATFORM_ROPHIVE_COMPUTE_WORKER_H

#include <atomic>
#include <cstddef>
#include <memory>

#include "hive.h"

namespace RopHive {

class Hive;

class ComputeWorker : public std::enable_shared_from_this<ComputeWorker> {
public:
    using TaskFn = Hive::TaskFn;

    ComputeWorker();
    explicit ComputeWorker(const Hive::Options& options);
    ~ComputeWorker() = default;

    ComputeWorker(const ComputeWorker&) = delete;
    ComputeWorker& operator=(const ComputeWorker&) = delete;

    void bind(Hive& hive, size_t worker_id);
    void requestStop();
    void run();

    size_t id() const noexcept { return worker_id_; }
    bool isSleeping() const noexcept { return sleeping_.load(std::memory_order_acquire); }

private:
    Hive* hive_{nullptr};
    size_t worker_id_{0};
    Hive::Options options_;
    bool initialized_{false};

    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> sleeping_{false};
};

} // namespace RopHive

#endif // _ROP_PLATFORM_ROPHIVE_COMPUTE_WORKER_H

