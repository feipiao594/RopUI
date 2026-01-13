#ifndef _ROP_PLATFORM_ROPHIVE_HIVE_WORKER_H
#define _ROP_PLATFORM_ROPHIVE_HIVE_WORKER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <utility>
#include <vector>

#ifdef __linux__
#include <sys/epoll.h>
#endif

#include "eventloop_core.h"
#include "hive.h"

#include "../../utils/concurrency/task_deque.h"

namespace RopHive {

class Hive;
class IWorkerWatcher;
class IWorkerWakeUpWatcher;

template <class T>
class MPSCQueue {
public:
    void push(T v) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            q_.push_back(std::move(v));
        }
        approx_size_.fetch_add(1, std::memory_order_release);
    }

    void drain(std::vector<T>& out) {
        std::deque<T> local;
        {
            std::lock_guard<std::mutex> lk(mu_);
            local.swap(q_);
        }
        if (local.empty()) {
            return;
        }
        approx_size_.fetch_sub(local.size(), std::memory_order_release);
        while (!local.empty()) {
            out.push_back(std::move(local.front()));
            local.pop_front();
        }
    }

    size_t approxSize() const noexcept {
        return approx_size_.load(std::memory_order_acquire);
    }

private:
    mutable std::mutex mu_;
    std::deque<T> q_;
    std::atomic<size_t> approx_size_{0};
};

class IOWorker : public std::enable_shared_from_this<IOWorker> {
public:
    using TaskFn = Hive::TaskFn;
    using Clock = Hive::Clock;
    using TimePoint = Hive::TimePoint;

    struct InboundCommand {
        enum class Kind { PrivateTask, AddTimer, StopWorker };

        Kind kind;
        TaskFn task;
        TimePoint deadline{};
    };

    IOWorker();
    explicit IOWorker(const Hive::Options& options);
    ~IOWorker();

    IOWorker(const IOWorker&) = delete;
    IOWorker& operator=(const IOWorker&) = delete;

    static std::optional<size_t> currentWorkerId() noexcept;
    static IOWorker* currentWorker() noexcept;

    size_t id() const noexcept { return worker_id_; }

    void bind(Hive& hive, size_t worker_id);
    void requestStop();
    void run();
    void wakeup();
    
    void postPrivate(TaskFn task);
    void postToLocal(TaskFn task);
    void addTimer(TimePoint deadline, TaskFn task);
    std::optional<TaskFn> tryStealTop();

    bool isSleeping() const noexcept { return sleeping_.load(std::memory_order_acquire); }
    uint32_t timerApproxSize() const noexcept { return timer_count_.load(std::memory_order_acquire); }

protected:
    friend class ::RopHive::IWorkerWatcher;

    void attachSource(std::shared_ptr<IEventSource> source);
    void detachSource(std::shared_ptr<IEventSource> source);

private:
    struct TimerTask {
        TimePoint deadline;
        TaskFn task;

        bool operator>(const TimerTask& rhs) const { return deadline > rhs.deadline; }
    };

private:
    int computeNextTimeoutMs() const;
    bool hasImmediateWork() const;

    void drainInbound();
    void runPrivateTasks();
    void runExpiredTimers();
    bool runLocalBatch();
    bool harvestGlobalBatch();
    bool stealOnce();

private:
    Hive* hive_{nullptr};
    size_t worker_id_{0};
    Hive::Options options_;
    bool initialized_{false};

    std::unique_ptr<IEventLoopCore> core_;
    std::unique_ptr<IWorkerWakeUpWatcher> wakeup_;

    MPSCQueue<InboundCommand> inbound_;

    std::deque<TaskFn> private_queue_;
    RopUI::Utils::Concurrency::TaskDeque local_dq_;

    std::priority_queue<TimerTask, std::vector<TimerTask>, std::greater<>> timers_;
    std::atomic<uint32_t> timer_count_{0};

    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> sleeping_{false};

    mutable std::mt19937 rng_;
};

} // namespace RopHive

#endif // _ROP_PLATFORM_ROPHIVE_HIVE_WORKER_H
