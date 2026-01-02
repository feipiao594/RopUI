#include <algorithm>
#include <random>
#include <stdexcept>

#include <log.hpp>

#include "schedule/hive.h"
#include "schedule/compute_worker.h"
#include "schedule/io_worker.h"

namespace RopHive {

void Hive::GlobalMicroPool::push(TaskFn task) {
    if (!task) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(std::move(task));
    }
    approx_size_.fetch_add(1, std::memory_order_release);
}

size_t Hive::GlobalMicroPool::tryPopBatch(std::vector<TaskFn>& out, size_t max_n) {
    out.clear();
    if (max_n == 0) return 0;

    std::deque<TaskFn> local;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (q_.empty()) return 0;
        const size_t n = std::min(max_n, q_.size());
        for (size_t i = 0; i < n; ++i) {
            local.push_back(std::move(q_.front()));
            q_.pop_front();
        }
    }

    approx_size_.fetch_sub(local.size(), std::memory_order_release);
    out.reserve(local.size());
    while (!local.empty()) {
        out.push_back(std::move(local.front()));
        local.pop_front();
    }
    return out.size();
}

void Hive::GlobalComputePool::push(TaskFn task) {
    if (!task) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        q_.push_back(std::move(task));
    }
    approx_size_.fetch_add(1, std::memory_order_release);
    cv_.notify_one();
}

size_t Hive::GlobalComputePool::waitPopBatch(std::vector<TaskFn>& out,
                                            size_t max_n,
                                            const std::atomic<bool>& exit_requested) {
    out.clear();
    if (max_n == 0) return 0;

    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait(lk, [&] {
        return !q_.empty() || exit_requested.load(std::memory_order_acquire);
    });
    if (q_.empty()) {
        return 0;
    }

    const size_t n = std::min(max_n, q_.size());
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(std::move(q_.front()));
        q_.pop_front();
    }
    lk.unlock();

    approx_size_.fetch_sub(n, std::memory_order_release);
    return n;
}

Hive::Hive()
    : options_() {}

Hive::Hive(Options options)
    : options_(std::move(options)) {}

Hive::~Hive() {
    requestExit();
    for (auto& t : io_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    for (auto& t : compute_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

size_t Hive::attachIOWorker(const std::shared_ptr<IOWorker>& worker) {
    if (started_.load(std::memory_order_acquire)) {
        throw std::runtime_error("Hive::attachIOWorker: attaching after run() not supported yet");
    }
    if (!worker) {
        throw std::runtime_error("Hive::attachIOWorker: worker is null");
    }

    std::lock_guard<std::mutex> lk(io_workers_mu_);
    const size_t id = io_workers_.size();
    worker->bind(*this, id);
    io_workers_.push_back(worker);
    return id;
}

size_t Hive::attachComputeWorker(const std::shared_ptr<ComputeWorker>& worker) {
    if (started_.load(std::memory_order_acquire)) {
        throw std::runtime_error("Hive::attachComputeWorker: attaching after run() not supported yet");
    }
    if (!worker) {
        throw std::runtime_error("Hive::attachComputeWorker: worker is null");
    }

    std::lock_guard<std::mutex> lk(compute_mu_);
    const size_t id = compute_workers_.size();
    worker->bind(*this, id);
    compute_workers_.push_back(worker);
    return id;
}

size_t Hive::IOWorkerCount() const noexcept {
    std::lock_guard<std::mutex> lk(io_workers_mu_);
    return io_workers_.size();
}

std::shared_ptr<IOWorker> Hive::workerAt(size_t idx) const {
    std::lock_guard<std::mutex> lk(io_workers_mu_);
    if (idx >= io_workers_.size()) {
        return {};
    }
    return io_workers_[idx];
}

size_t Hive::tryPopGlobalMicroBatch(std::vector<TaskFn>& out, size_t max_n) {
    return global_micro_.tryPopBatch(out, max_n);
}

size_t Hive::globalMicroApproxSize() const noexcept {
    return global_micro_.approxSize();
}

size_t Hive::tryPopGlobalComputeBatch(std::vector<TaskFn>& out, size_t max_n) {
    return global_compute_.waitPopBatch(out, max_n, exit_requested_);
}

void Hive::wakeOneIdleIOWorker() {
    std::vector<std::shared_ptr<IOWorker>> workers;
    {
        std::lock_guard<std::mutex> lk(io_workers_mu_);
        workers = io_workers_;
    }

    for (auto& w : workers) {
        if (w && w->isSleeping()) {
            w->wakeup();
            return;
        }
    }
}

std::shared_ptr<IOWorker> Hive::pickDelayedWorker() {
    std::vector<std::shared_ptr<IOWorker>> workers;
    {
        std::lock_guard<std::mutex> lk(io_workers_mu_);
        workers = io_workers_;
    }
    if (workers.empty()) {
        return {};
    }
    if (workers.size() == 1) {
        return workers[0];
    }

    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, workers.size() - 1);

    auto pickA = dist(rng);
    auto pickB = dist(rng);
    if (pickA == pickB) {
        pickB = (pickB + 1) % workers.size();
    }
    auto& a = workers[pickA];
    auto& b = workers[pickB];
    if (!a) return b;
    if (!b) return a;
    return (a->timerApproxSize() <= b->timerApproxSize()) ? a : b;
}

void Hive::postIO(TaskFn task) {
    if (!task) return;
    global_micro_.push(std::move(task));
    wakeOneIdleIOWorker();
}

void Hive::postDelayed(TaskFn task, Duration delay) {
    if (!task) return;
    if (delay.count() < 0) {
        delay = Duration(0);
    }

    auto worker = pickDelayedWorker();
    if (!worker) {
        LOG(WARN)("Hive::postDelayed: no worker attached");
        return;
    }

    worker->addTimer(Clock::now() + delay, std::move(task));
    worker->wakeup();
}

void Hive::postToWorker(size_t worker_id, TaskFn task) {
    if (!task) return;
    auto w = workerAt(worker_id);
    if (!w) {
        LOG(WARN)("Hive::postToWorker: invalid worker id %zu", worker_id);
        return;
    }
    w->postPrivate(std::move(task));
    w->wakeup();
}

void Hive::postCompute(TaskFn task) {
    if (!task) return;
    global_compute_.push(std::move(task));
}

void Hive::requestExit() {
    const bool already = exit_requested_.exchange(true, std::memory_order_acq_rel);
    if (already) return;

    std::vector<std::shared_ptr<IOWorker>> workers;
    {
        std::lock_guard<std::mutex> lk(io_workers_mu_);
        workers = io_workers_;
    }

    for (auto& w : workers) {
        if (w) {
            w->requestStop();
            w->wakeup();
        }
    }

    std::vector<std::shared_ptr<ComputeWorker>> compute_workers;
    {
        std::lock_guard<std::mutex> lk(compute_mu_);
        compute_workers = compute_workers_;
    }
    for (auto& w : compute_workers) {
        if (w) {
            w->requestStop();
        }
    }
    global_compute_.cv_.notify_all();
}

void Hive::run() {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        throw std::runtime_error("Hive::run: already started");
    }

    std::vector<std::shared_ptr<IOWorker>> io_workers;
    {
        std::lock_guard<std::mutex> lk(io_workers_mu_);
        io_workers = io_workers_;
    }
    if (io_workers.empty()) {
        throw std::runtime_error("Hive::run: requires at least one worker");
    }

    // Start all workers and spawn threads for the non-main workers.
    for (size_t i = 0; i < io_workers.size(); ++i) {
        if (!io_workers[i]) continue;
        if (i == 0) continue;
        io_threads_.emplace_back([w = io_workers[i]] {
            w->run();
        });
    }

    // Start compute workers (all on background threads).
    std::vector<std::shared_ptr<ComputeWorker>> compute_workers;
    {
        std::lock_guard<std::mutex> lk(compute_mu_);
        compute_workers = compute_workers_;
    }
    for (auto& w : compute_workers) {
        if (!w) continue;
        compute_threads_.emplace_back([w] { w->run(); });
    }

    // Main worker runs on the calling thread.
    io_workers[0]->run();

    // Ensure other workers are stopped and joined.
    requestExit();
    for (auto& t : io_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    io_threads_.clear();

    for (auto& t : compute_threads_) {
        if (t.joinable()) {
            t.join();
        }
    }
    compute_threads_.clear();
}

} // namespace RopHive
