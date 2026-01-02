#include "test_util.h"

#include "platform/schedule/hive.h"
#include "platform/schedule/compute_worker.h"
#include "platform/schedule/io_worker.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

static void test_post_and_post_delayed() {
    RopHive::Hive hive;
    hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));

    std::atomic<int> counter{0};

    std::thread runner([&] { hive.run(); });

    hive.postIO([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    hive.postDelayed([&] {
        counter.fetch_add(10, std::memory_order_relaxed);
        hive.requestExit();
    }, 10ms);

    runner.join();

    ROPUI_TEST_ASSERT(counter.load(std::memory_order_relaxed) == 11);
}

static void test_post_to_worker_private_queue() {
    RopHive::Hive hive;
    const size_t id0 = hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));

    std::atomic<int> counter{0};
    std::thread runner([&] { hive.run(); });

    hive.postToWorker(id0, [&] { counter.fetch_add(7, std::memory_order_relaxed); });
    hive.postDelayed([&] { hive.requestExit(); }, 10ms);

    runner.join();
    ROPUI_TEST_ASSERT(counter.load(std::memory_order_relaxed) == 7);
}

static void test_compute_pool_runs_tasks() {
    RopHive::Hive hive;
    hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));
    hive.attachComputeWorker(std::make_shared<RopHive::ComputeWorker>(hive.options()));

    std::atomic<int> counter{0};
    std::thread runner([&] { hive.run(); });

    hive.postCompute([&] { counter.fetch_add(5, std::memory_order_relaxed); });
    hive.postDelayed([&] {
        hive.requestExit();
    }, 10ms);

    runner.join();
    ROPUI_TEST_ASSERT(counter.load(std::memory_order_relaxed) == 5);
}

static void test_io_poll_backend_runs() {
    RopHive::Hive::Options options;
    options.io_backend = RopHive::BackendType::LINUX_POLL;
    RopHive::Hive hive(options);

    hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));

    std::atomic<int> counter{0};
    std::thread runner([&] { hive.run(); });

    hive.postIO([&] { counter.fetch_add(1, std::memory_order_relaxed); });
    hive.postDelayed([&] { hive.requestExit(); }, 10ms);

    runner.join();
    ROPUI_TEST_ASSERT(counter.load(std::memory_order_relaxed) == 1);
}

static void test_exit_when_idle() {
    RopHive::Hive hive;
    hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));

    std::thread runner([&] { hive.run(); });
    std::this_thread::sleep_for(10ms);
    hive.requestExit();
    runner.join();
}

static void test_concurrent_post_no_loss() {
    RopHive::Hive hive;
    hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));
    hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));
    hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));

    constexpr int kThreads = 8;
    constexpr int kPerThread = 2000;
    const int expected = kThreads * kPerThread;

    std::atomic<int> counter{0};

    std::thread runner([&] { hive.run(); });

    std::vector<std::thread> producers;
    producers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                hive.postIO([&] { counter.fetch_add(1, std::memory_order_relaxed); });
            }
        });
    }
    for (auto& th : producers) {
        th.join();
    }

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (counter.load(std::memory_order_relaxed) < expected &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    ROPUI_TEST_ASSERT(counter.load(std::memory_order_relaxed) == expected);

    hive.requestExit();
    runner.join();
}

static void test_post_to_worker_hits_target_worker() {
    RopHive::Hive hive;
    const size_t id0 = hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));
    const size_t id1 = hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));

    std::atomic<int> ok{0};
    std::thread runner([&] { hive.run(); });

    hive.postToWorker(id0, [&] {
        auto wid = RopHive::IOWorker::currentWorkerId();
        if (wid.has_value() && *wid == id0) {
            ok.fetch_add(1, std::memory_order_relaxed);
        }
    });

    hive.postToWorker(id1, [&] {
        auto wid = RopHive::IOWorker::currentWorkerId();
        if (wid.has_value() && *wid == id1) {
            ok.fetch_add(1, std::memory_order_relaxed);
        }
    });

    hive.postDelayed([&] { hive.requestExit(); }, 20ms);
    runner.join();
    ROPUI_TEST_ASSERT(ok.load(std::memory_order_relaxed) == 2);
}

int main() {
    RopUI::Tests::run("hive_epoll.post_and_post_delayed", test_post_and_post_delayed);
    RopUI::Tests::run("hive_epoll.post_to_worker_private_queue", test_post_to_worker_private_queue);
    RopUI::Tests::run("hive_compute_pool.runs_tasks", test_compute_pool_runs_tasks);
    RopUI::Tests::run("hive_poll.io_backend_runs", test_io_poll_backend_runs);
    RopUI::Tests::run("hive_core.exit_when_idle", test_exit_when_idle);
    RopUI::Tests::run("hive_core.concurrent_post_no_loss", test_concurrent_post_no_loss);
    RopUI::Tests::run("hive_core.post_to_worker_hits_target", test_post_to_worker_hits_target_worker);
    return 0;
}
