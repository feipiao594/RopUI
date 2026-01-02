#include "schedule/compute_worker.h"

#include <stdexcept>

namespace RopHive {

ComputeWorker::ComputeWorker()
    : options_() {}

ComputeWorker::ComputeWorker(const Hive::Options& options)
    : options_(options) {}

void ComputeWorker::bind(Hive& hive, size_t worker_id) {
    if (initialized_) {
        throw std::runtime_error("ComputeWorker::bind: already bound");
    }
    hive_ = &hive;
    worker_id_ = worker_id;
    initialized_ = true;
}

void ComputeWorker::requestStop() {
    stop_requested_.store(true, std::memory_order_release);
}

void ComputeWorker::run() {
    if (!initialized_ || hive_ == nullptr) {
        throw std::runtime_error("ComputeWorker::run: worker not bound to a Hive");
    }

    std::vector<TaskFn> batch;
    batch.reserve(options_.compute_batch_size);

    while (!stop_requested_.load(std::memory_order_acquire) && !hive_->getExitRequested()) {
        sleeping_.store(true, std::memory_order_release);
        const size_t n = hive_->tryPopGlobalComputeBatch(batch, options_.compute_batch_size);
        if (n == 0) {
            continue;
        }
        sleeping_.store(false, std::memory_order_release);


        for (auto& task : batch) {
            if (stop_requested_.load(std::memory_order_acquire) || hive_->getExitRequested()) {
                break;
            }
            if (task) {
                task();
            }
        }
    }
}

} // namespace RopHive

