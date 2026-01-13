#include "schedule/io_worker.h"

#include <cerrno>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <functional>

#include <log.hpp>

#include "schedule/worker_watcher.h"
#include "schedule/sched_trace.h"

#ifdef __linux__
#include "linux/schedule/watcher/epoll_worker_wakeup.h"
#include "linux/schedule/watcher/poll_worker_wakeup.h"
#endif

#ifdef __APPLE__
#include "macos/schedule/watcher/kqueue_worker_wakeup.h"
#include "macos/schedule/watcher/cocoa_worker_wakeup.h"
#endif

#ifdef _WIN32
#include "windows/schedule/watcher/iocp_worker_wakeup.h"
#include "windows/schedule/watcher/win32_worker_wakeup.h"
#endif

namespace RopHive {

static thread_local IOWorker* tls_worker = nullptr;
static thread_local size_t tls_worker_id = 0;
static thread_local bool tls_worker_valid = false;

std::optional<size_t> IOWorker::currentWorkerId() noexcept {
    if (!tls_worker_valid) {
        return std::nullopt;
    }
    return tls_worker_id;
}

IOWorker* IOWorker::currentWorker() noexcept {
    return tls_worker;
}

IOWorker::IOWorker()
    : options_(),
      local_dq_(options_.local_queue_capacity),
      rng_(std::random_device{}()) {}

IOWorker::IOWorker(const Hive::Options& options)
    : options_(options),
      local_dq_(options_.local_queue_capacity),
      rng_(std::random_device{}()) {}

IOWorker::~IOWorker() {
    requestStop();
    if (wakeup_) {
        wakeup_->stop();
        wakeup_.reset();
    }
}

void IOWorker::bind(Hive& hive, size_t worker_id) {
    if (initialized_) {
        throw std::runtime_error("IOWorker::bind: already bound");
    }

    hive_ = &hive;
    worker_id_ = worker_id;

    SCHED_TRACE_E(worker_id_,
                 ::RopHive::SchedTrace::Event::WorkerBind,
                 "backend=%d local_cap=%zu local_batch=%zu global_batch=%zu steal_batch=%zu",
                 static_cast<int>(options_.io_backend),
                 options_.local_queue_capacity,
                 options_.local_batch_size,
                 options_.global_batch_size,
                 options_.steal_batch_size);

    core_ = createEventLoopCore(options_.io_backend);
    if (!core_) {
        throw std::runtime_error("IOWorker: createEventLoopCore returned null");
    }

    initialized_ = true;

#ifdef __linux__
    if (options_.io_backend == BackendType::LINUX_EPOLL) {
        wakeup_ = std::make_unique<RopHive::Linux::EpollWorkerWakeUpWatcher>(*this);
    } else if (options_.io_backend == BackendType::LINUX_POLL) {
        wakeup_ = std::make_unique<RopHive::Linux::PollWorkerWakeUpWatcher>(*this);
    } else {
        throw std::runtime_error("IOWorker: unsupported linux backend");
    }
#elif defined(__APPLE__)
    if (options_.io_backend == BackendType::MACOS_KQUEUE) {
        wakeup_ = std::make_unique<RopHive::MacOS::KqueueWorkerWakeUpWatcher>(*this);
    } else if (options_.io_backend == BackendType::MACOS_COCOA) {
        wakeup_ = std::make_unique<RopHive::MacOS::CocoaWorkerWakeUpWatcher>(*this);
    } else if (options_.io_backend == BackendType::MACOS_POLL) {
        // Existing poll backend uses the EventLoop watcher only; worker wakeup can be added later.
        wakeup_.reset();
    } else {
        throw std::runtime_error("IOWorker: unsupported macos backend");
    }
#elif defined(_WIN32)
    if (options_.io_backend == BackendType::WINDOWS_IOCP) {
        wakeup_ = std::make_unique<RopHive::Windows::IocpWorkerWakeUpWatcher>(*this);
    } else if (options_.io_backend == BackendType::WINDOWS_WIN32) {
        wakeup_ = std::make_unique<RopHive::Windows::Win32WorkerWakeUpWatcher>(*this);
    } else {
        throw std::runtime_error("IOWorker: unsupported windows backend");
    }
#else
    throw std::runtime_error("IOWorker: unsupported platform");
#endif
}

void IOWorker::requestStop() {
    stop_requested_.store(true, std::memory_order_release);
}

void IOWorker::postPrivate(TaskFn task) {
    if (!task) return;
    inbound_.push(InboundCommand{
        .kind = InboundCommand::Kind::PrivateTask,
        .task = std::move(task),
    });
}

#define DEBUG
#ifdef DEBUG
void IOWorker::postToLocal(TaskFn task) {
    // this function only for debug
    if (!task) return;

    // Only the owner worker thread should push into its local deque.
    // For non-owner threads, degrade to the global pool to stay safe.
    if (IOWorker::currentWorker() != this) {
        if (hive_) {
            SCHED_TRACE_E(worker_id_, ::RopHive::SchedTrace::Event::LocalDegradeGlobal, "reason=non_owner");
            hive_->postIO(std::move(task));
            return;
        }
        task();
        return;
    }

    if (local_dq_.remainingSpace() != 0 && local_dq_.tryPushBottom(std::move(task))) {
        const size_t local_sz = local_dq_.approxSize();
        SCHED_TRACE_E(worker_id_, ::RopHive::SchedTrace::Event::LocalPush, "local=%zu", local_sz);
        return;
    }

    if (hive_) {
        SCHED_TRACE_E(worker_id_,
                     ::RopHive::SchedTrace::Event::LocalDegradeGlobal,
                     "reason=full local=%zu",
                     local_dq_.approxSize());
        hive_->postIO(std::move(task));
        return;
    }

    // No hive bound; execute inline as a last resort.
    task();
}
#endif

void IOWorker::addTimer(TimePoint deadline, TaskFn task) {
    if (!task) return;
    inbound_.push(InboundCommand{
        .kind = InboundCommand::Kind::AddTimer,
        .task = std::move(task),
        .deadline = deadline,
    });
}

void IOWorker::wakeup() {
    SCHED_TRACE_E0(worker_id_, ::RopHive::SchedTrace::Event::WakeupNotify);
    if (wakeup_) {
        wakeup_->notify();
    }
}

std::optional<IOWorker::TaskFn> IOWorker::tryStealTop() {
    return local_dq_.tryStealTop();
}

void IOWorker::attachSource(std::shared_ptr<IEventSource> source) {
    if (!initialized_ || !core_ || !source) {
        return;
    }
    core_->addSource(std::move(source));
    core_->applyInitialChanges();
}

void IOWorker::detachSource(std::shared_ptr<IEventSource> source) {
    if (!initialized_ || !core_ || !source) {
        return;
    }
    core_->removeSource(std::move(source));
    core_->applyInitialChanges();
}

bool IOWorker::hasImmediateWork() const {
    if (!hive_) {
        return true;
    }
    if (stop_requested_.load(std::memory_order_acquire) || hive_->getExitRequested()) {
        return true;
    }
    if (inbound_.approxSize() != 0) return true;
    if (!private_queue_.empty()) return true;
    if (local_dq_.approxSize() != 0) return true;
    if (hive_->globalMicroApproxSize() != 0) return true;
    return false;
}

int IOWorker::computeNextTimeoutMs() const {
    if (!hive_) {
        return 0;
    }
    if (stop_requested_.load(std::memory_order_acquire) || hive_->getExitRequested()) {
        return 0;
    }
    if (hasImmediateWork()) {
        return 0;
    }
    if (timers_.empty()) {
        return -1;
    }

    const auto now = Clock::now();
    const auto deadline = timers_.top().deadline;
    if (deadline <= now) {
        return 0;
    }

    const auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    const auto ms = delta.count();
    if (ms > static_cast<decltype(ms)>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(ms);
}

void IOWorker::drainInbound() {
    std::vector<InboundCommand> inbound_tmp_;
    inbound_tmp_.clear();
    inbound_.drain(inbound_tmp_);

    if (inbound_tmp_.empty()) {
        return;
    }

    for (auto& cmd : inbound_tmp_) {
        switch (cmd.kind) {
        case InboundCommand::Kind::PrivateTask:
            if (cmd.task) {
                private_queue_.push_back(std::move(cmd.task));
            }
            break;
        case InboundCommand::Kind::AddTimer:
            if (cmd.task) {
                timers_.push(TimerTask{cmd.deadline, std::move(cmd.task)});
                timer_count_.fetch_add(1, std::memory_order_release);
            }
            break;
        case InboundCommand::Kind::StopWorker:
            stop_requested_.store(true, std::memory_order_release);
            break;
        }
    }

    SCHED_TRACE_E(worker_id_,
                 ::RopHive::SchedTrace::Event::InboundDrained,
                 "n=%zu inbound=%zu",
                 inbound_tmp_.size(),
                 inbound_.approxSize());
}

void IOWorker::runPrivateTasks() {
    size_t ran = 0;
    while (!private_queue_.empty()) {
        TaskFn task = std::move(private_queue_.front());
        private_queue_.pop_front();
        if (task) {
            task();
        }
        ran += 1;
        if (stop_requested_.load(std::memory_order_acquire) || (hive_ && hive_->getExitRequested())) {
            break;
        }
    }
    if (ran != 0) {
        SCHED_TRACE_E(worker_id_, ::RopHive::SchedTrace::Event::PrivateDone, "n=%zu", ran);
    }
}

void IOWorker::runExpiredTimers() {
    const auto now = Clock::now();
    size_t ran = 0;

    while (!timers_.empty() && timers_.top().deadline <= now) {
        TaskFn task = std::move(timers_.top().task);
        timers_.pop();
        timer_count_.fetch_sub(1, std::memory_order_release);
        if (task) {
            task();
        }
        ran += 1;
        if (stop_requested_.load(std::memory_order_acquire) || (hive_ && hive_->getExitRequested())) {
            break;
        }
    }
    if (ran != 0) {
        SCHED_TRACE_E(worker_id_, ::RopHive::SchedTrace::Event::TimerDone, "n=%zu", ran);
    }
}

bool IOWorker::runLocalBatch() {
    const size_t budget = options_.local_batch_size;
    size_t ran = 0;
    for (size_t i = 0; i < budget; ++i) {
        auto opt = local_dq_.tryPopBottom();
        if (!opt.has_value()) {
            break;
        }
        auto task = std::move(*opt);
        ran += 1;
        if (task) {
            task();
        }
        if (stop_requested_.load(std::memory_order_acquire) || (hive_ && hive_->getExitRequested())) {
            break;
        }
    }
    if (ran != 0) {
        SCHED_TRACE_E(worker_id_,
                     ::RopHive::SchedTrace::Event::LocalDone,
                     "n=%zu local=%zu",
                     ran,
                     local_dq_.approxSize());
    }
    return ran != 0;
}

bool IOWorker::harvestGlobalBatch() {
    if (!hive_) return false;
    std::vector<TaskFn> batch;
    const size_t n = hive_->tryPopGlobalMicroBatch(batch, options_.global_batch_size);
    if (n == 0) return false;

    size_t moved_or_ran = 0;
    for (auto& task : batch) {
        if (!task) continue;
        TaskFn local_task = std::move(task);
        if (local_dq_.remainingSpace() != 0 && local_dq_.tryPushBottom(std::move(local_task))) {
            moved_or_ran += 1;
            continue;
        }
        // Defensive code, the local_dq_ here is always empty
        // Local deque is full (or contention); execute inline to avoid dropping work.
        if (local_task) {
            local_task();
        }
        moved_or_ran += 1;
        if (stop_requested_.load(std::memory_order_acquire) || (hive_ && hive_->getExitRequested())) {
            break;
        }
    }
    if (moved_or_ran != 0) {
        SCHED_TRACE_E(worker_id_,
                     ::RopHive::SchedTrace::Event::GlobalHarvestDone,
                     "n=%zu local=%zu",
                     moved_or_ran,
                     local_dq_.approxSize());
    }
    return moved_or_ran != 0;
}

bool IOWorker::stealOnce() {
    if (!hive_) return false;
    const size_t n = hive_->IOWorkerCount();
    if (n <= 1) return false;
    if (options_.steal_batch_size == 0) return false;

    std::uniform_int_distribution<size_t> dist(0, n - 1);

    constexpr size_t kTries = 4;
    for (size_t i = 0; i < kTries; ++i) {
        const size_t victim_id = dist(rng_);
        if (victim_id == worker_id_) {
            continue;
        }
        auto victim = hive_->workerAt(victim_id);
        if (!victim) continue;

        const size_t steal_n = std::max<size_t>(1, options_.steal_batch_size);
        size_t stole_cnt = 0;
        size_t victim_sz = 0;

        if (RopHive::SchedTrace::enabled())
            victim_sz = victim->local_dq_.approxSize();

        if (RopHive::SchedTrace::enabled() && victim_sz != 0) {
            const auto st = victim->local_dq_.debugState();
            SCHED_TRACE_E(worker_id_,
                         ::RopHive::SchedTrace::Event::StealAttempt,
                         "victim=%zu victim_local=%zu v_top=%zu v_bottom=%zu v_size=%zu",
                         victim_id,
                         victim_sz,
                         st.top,
                         st.bottom,
                         st.size);
        }

        for (size_t j = 0; j < steal_n; ++j) {
            auto stolen = victim->tryStealTop();
            if (!stolen.has_value()) {
                break;
            }
            
            // Prefer to enqueue locally, fallback to inline exec.
            TaskFn task = std::move(*stolen);
            if (local_dq_.remainingSpace() != 0 && local_dq_.tryPushBottom(std::move(task))) {
                stole_cnt += 1;
                continue;
            }
            if (task) {
                task();
            }
            
            stole_cnt += 1;
        }

        if (stole_cnt != 0) {
            SCHED_TRACE_E(worker_id_,
                         ::RopHive::SchedTrace::Event::StealSuccess,
                         "victim=%zu stolen=%zu local=%zu victim_local=%zu",
                         victim_id,
                         stole_cnt,
                         local_dq_.approxSize(),
                         victim->local_dq_.approxSize());
            return true;
        }

        if (RopHive::SchedTrace::enabled() && victim_sz != 0) {
            const auto st = victim->local_dq_.debugState();
            SCHED_TRACE_E(worker_id_,
                         ::RopHive::SchedTrace::Event::StealFail,
                         "victim=%zu victim_local=%zu v_top=%zu v_bottom=%zu v_size=%zu",
                         victim_id,
                         victim_sz,
                         st.top,
                         st.bottom,
                         st.size);
        }
    }
    return false;
}

void IOWorker::run() {
    if (!initialized_) {
        throw std::runtime_error("IOWorker::run: worker not bound to a Hive");
    }

    tls_worker = this;
    tls_worker_id = worker_id_;
    tls_worker_valid = true;

    const auto tid = static_cast<unsigned long long>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    SCHED_TRACE_E(worker_id_, ::RopHive::SchedTrace::Event::WorkerRunStart, "tid=%llu", tid);

    if (wakeup_) {
        wakeup_->start();
    }

    while (!stop_requested_.load(std::memory_order_acquire) && !hive_->getExitRequested()) {
        if (RopHive::SchedTrace::enabled()) {
            static thread_local uint32_t tick_seq = 0;
            tick_seq += 1;
            if ((tick_seq & 0x3FF) == 0) {
                SCHED_TRACE_E(worker_id_,
                             ::RopHive::SchedTrace::Event::LoopTick,
                             "inbound=%zu private=%zu local=%zu global=%zu timers=%u",
                             inbound_.approxSize(),
                             private_queue_.size(),
                             local_dq_.approxSize(),
                             hive_ ? hive_->globalMicroApproxSize() : 0,
                             timer_count_.load(std::memory_order_acquire));
                tick_seq = 0;
            }
        }

        const int timeout_ms = computeNextTimeoutMs();

        const bool will_block = (timeout_ms != 0);
        if (will_block) {
            sleeping_.store(true, std::memory_order_release);
            SCHED_TRACE_E(worker_id_, ::RopHive::SchedTrace::Event::SleepEnter, "timeout_ms=%d", timeout_ms);
        }

        if (RopHive::SchedTrace::enabled()) {
            static thread_local uint32_t runonce_seq = 0;
            runonce_seq += 1;
            if ((runonce_seq & 0xFF) == 0) {
                SCHED_TRACE_E(worker_id_,
                             ::RopHive::SchedTrace::Event::RunOnceBegin,
                             "timeout_ms=%d inbound=%zu private=%zu local=%zu global=%zu timers=%u",
                             timeout_ms,
                             inbound_.approxSize(),
                             private_queue_.size(),
                             local_dq_.approxSize(),
                             hive_ ? hive_->globalMicroApproxSize() : 0,
                             timer_count_.load(std::memory_order_acquire));
                runonce_seq = 0;
            }
        }

        core_->runOnce(timeout_ms);

        if (RopHive::SchedTrace::enabled()) {
            static thread_local uint32_t runonce_end_seq = 0;
            runonce_end_seq += 1;
            if ((runonce_end_seq & 0xFF) == 0) {
                SCHED_TRACE_E(worker_id_, ::RopHive::SchedTrace::Event::RunOnceEnd, "timeout_ms=%d", timeout_ms);
                runonce_end_seq = 0;
            }
        }

        if (will_block) {
            sleeping_.store(false, std::memory_order_release);
            SCHED_TRACE_E0(worker_id_, ::RopHive::SchedTrace::Event::SleepExit);
        }

        drainInbound();
        runPrivateTasks();
        runExpiredTimers();

        if (stop_requested_.load(std::memory_order_acquire) || hive_->getExitRequested()) {
            break;
        }

        if (runLocalBatch()) {
            continue;
        }
        if (harvestGlobalBatch()) {
            continue;
        }
        if (stealOnce()) {
            continue;
        }
    }

    SCHED_TRACE_E(worker_id_, ::RopHive::SchedTrace::Event::WorkerRunStop, "tid=%llu", tid);
    tls_worker_valid = false;
    tls_worker_id = 0;
    tls_worker = nullptr;
}

} // namespace RopHive
