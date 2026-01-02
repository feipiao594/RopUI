#ifndef _ROP_PLATFORM_ROPHIVE_HIVE_H
#define _ROP_PLATFORM_ROPHIVE_HIVE_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "eventloop.h"

namespace RopHive {

class IOWorker;
class ComputeWorker;

class Hive {
public:
    using TaskFn = std::function<void()>;
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = std::chrono::milliseconds;

    struct Options {
        BackendType io_backend = DEFAULT_BACKENDTYPE;
        size_t local_queue_capacity = 256;
        size_t global_batch_size = 16;
        size_t local_batch_size = 64;
        size_t steal_batch_size = 4;
        size_t compute_batch_size = 4;
    };

    Hive();
    explicit Hive(Options options);
    ~Hive();

    Hive(const Hive&) = delete;
    Hive& operator=(const Hive&) = delete;

    // Attaches an externally created worker to this Hive.
    // The first attached worker will run on the calling thread in run().
    size_t attachIOWorker(const std::shared_ptr<IOWorker>& worker);
    void postIO(TaskFn task);
    void postDelayed(TaskFn task, Duration delay);
    void postToWorker(size_t worker_id, TaskFn task);

    // Compute tasks are executed by ComputeWorker(s) from a shared global pool.
    // Compute tasks do not support delayed submission.
    size_t attachComputeWorker(const std::shared_ptr<ComputeWorker>& worker);
    void postCompute(TaskFn task);

    void requestExit();
    void run();

    bool getExitRequested() const noexcept {
        return exit_requested_.load(std::memory_order_acquire);
    }

    // Internal: used by IOWorker.
    size_t IOWorkerCount() const noexcept;
    std::shared_ptr<IOWorker> workerAt(size_t idx) const;

    // Internal: global micro task pool
    size_t tryPopGlobalMicroBatch(std::vector<TaskFn>& out, size_t max_n);
    size_t globalMicroApproxSize() const noexcept;

    // Internal: global compute task pool
    size_t tryPopGlobalComputeBatch(std::vector<TaskFn>& out, size_t max_n);

    const Options& options() const noexcept { return options_; }

private:
    void wakeOneIdleIOWorker();
    std::shared_ptr<IOWorker> pickDelayedWorker();

private:
    struct GlobalMicroPool {
        void push(TaskFn task);
        size_t tryPopBatch(std::vector<TaskFn>& out, size_t max_n);
        size_t approxSize() const noexcept { return approx_size_.load(std::memory_order_acquire); }

        mutable std::mutex mu_;
        std::deque<TaskFn> q_;
        std::atomic<size_t> approx_size_{0};
    };

    struct GlobalComputePool {
        void push(TaskFn task);
        size_t waitPopBatch(std::vector<TaskFn>& out,
                            size_t max_n,
                            const std::atomic<bool>& exit_requested);

        mutable std::mutex mu_;
        std::deque<TaskFn> q_;
        std::condition_variable cv_;
        std::atomic<size_t> approx_size_{0};
    };

    Options options_;

    mutable std::mutex io_workers_mu_;
    std::vector<std::shared_ptr<IOWorker>> io_workers_;
    std::vector<std::thread> io_threads_;

    mutable std::mutex compute_mu_;
    std::vector<std::shared_ptr<ComputeWorker>> compute_workers_;
    std::vector<std::thread> compute_threads_;

    GlobalMicroPool global_micro_;
    GlobalComputePool global_compute_;

    std::atomic<bool> started_{false};
    std::atomic<bool> exit_requested_{false};
};

} // namespace RopHive

#endif // _ROP_PLATFORM_ROPHIVE_HIVE_H
