#include "platform/schedule/hive.h"

#define DEBUG
#include "platform/schedule/io_worker.h"
#undef DEBUG

#include "platform/schedule/sched_trace.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <memory>
#include <thread>

#include <log.hpp>

using namespace std::chrono_literals;

static long long nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static void busyWork(std::chrono::microseconds us) {
    const auto end = std::chrono::steady_clock::now() + us;
    while (std::chrono::steady_clock::now() < end) {
        // spin
    }
}

int main(int argc, char** argv) {
    const char* out_path = nullptr;
    bool force_trace_on = false;
    bool force_trace_off = false;
    bool steal_enabled = true;
    int tasks = 20000;
    int workers = 4;
    int work_us = 200;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (std::strcmp(argv[i], "--sched-trace") == 0) {
            force_trace_on = true;
        } else if (std::strcmp(argv[i], "--no-sched-trace") == 0) {
            force_trace_off = true;
        } else if (std::strcmp(argv[i], "--no-steal") == 0) {
            steal_enabled = false;
        } else if (std::strcmp(argv[i], "--steal") == 0) {
            steal_enabled = true;
        } else if (std::strcmp(argv[i], "--tasks") == 0 && i + 1 < argc) {
            tasks = std::max(1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--workers") == 0 && i + 1 < argc) {
            workers = std::max(1, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--work-us") == 0 && i + 1 < argc) {
            work_us = std::max(0, std::atoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::fprintf(stderr,
                         "usage: %s [--log FILE] [--sched-trace|--no-sched-trace] [--steal|--no-steal]\n"
                         "          [--workers N] [--tasks N] [--work-us N]\n",
                         argv[0]);
            return 0;
        }
    }

    if (force_trace_on && force_trace_off) {
        std::fprintf(stderr, "invalid args: both --sched-trace and --no-sched-trace\n");
        return 2;
    }
    if (force_trace_on) {
        RopHive::SchedTrace::setEnabled(true);
    } else if (force_trace_off) {
        RopHive::SchedTrace::setEnabled(false);
    }

    if (out_path) {
        std::FILE* fp = std::fopen(out_path, "w");
        if (fp) {
            logger::setOutput(fp);
        }
    }
    logger::setOptions(LOG_OPT_LEVEL); // keep parsing easy
    logger::setMinLevel(RopHive::SchedTrace::enabled() ? LogLevel::DEBUG : LogLevel::INFO);

    RopHive::Hive::Options opt;
    opt.local_queue_capacity = 32768;
    opt.local_batch_size = 16;
    opt.global_batch_size = 16;
    opt.steal_batch_size = steal_enabled ? 1024 : 0;

    RopHive::Hive hive(opt); 

    for (int i = 0; i < workers; ++i) {
        hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));
    }

    std::thread runner([&] { hive.run(); });

    // Keep each worker waking up periodically (so it can reach the steal step).
    for (int wid = 0; wid < workers; ++wid) {
        hive.postToWorker(wid, [&, wid] {
            auto* self = RopHive::IOWorker::currentWorker();
            if (!self) return;

            auto tick = std::make_shared<RopHive::Hive::TaskFn>();
            *tick = [&, tick] {
                if (hive.getExitRequested()) return;
                auto* w = RopHive::IOWorker::currentWorker();
                if (!w) return;
                w->addTimer(RopHive::Hive::Clock::now() + 20ms, *tick);
            };
            self->addTimer(RopHive::Hive::Clock::now() + 20ms, *tick);
        });
    }

    std::atomic<int> remaining{tasks};
    std::atomic<bool> exit_scheduled{false};
    std::atomic<long long> done_ns{-1};

    // Seed on worker-0: generate a large local backlog so other workers must steal.
    const auto t0_ns = nowNs();
    hive.postToWorker(0, [&] {
        auto* w = RopHive::IOWorker::currentWorker();
        if (!w) return;
        for (int i = 0; i < tasks; ++i) {
            w->postToLocal([&] {
                if (work_us > 0) {
                    busyWork(std::chrono::microseconds(work_us));
                }
                if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    bool expected = false;
                    if (exit_scheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                        done_ns.store(nowNs(), std::memory_order_release);
                        hive.postDelayed([&] { hive.requestExit(); }, 200ms);
                    }
                }
            });
        }
        // Give other workers a window to steal before the victim starts consuming aggressively.
        SCHED_TRACE_E(w->id(), ::RopHive::SchedTrace::Event::UserBlockBegin, "reason=seed_sleep ms=200");
        std::this_thread::sleep_for(200ms);
        SCHED_TRACE_E(w->id(), ::RopHive::SchedTrace::Event::UserBlockEnd, "reason=seed_sleep ms=200");
    });

    runner.join();
    const auto t1_ns = nowNs();
    const auto done_at = done_ns.load(std::memory_order_acquire);
    const long long work_ms = (done_at > 0) ? ((done_at - t0_ns) / 1000000) : -1;
    const long long total_ms = (t1_ns - t0_ns) / 1000000;
    LOG(INFO)("work_ms=%lld total_ms=%lld steal=%s workers=%d tasks=%d work_us=%d",
              work_ms,
              total_ms,
              steal_enabled ? "on" : "off",
              workers,
              tasks,
              work_us);
    LOG(INFO)("done");
    return 0;
}
