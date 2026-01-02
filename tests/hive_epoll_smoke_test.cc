#include "test_util.h"

#include "platform/schedule/hive.h"
#include "platform/schedule/compute_worker.h"
#include "platform/schedule/io_worker.h"

#include <atomic>
#include <thread>

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

int main() {
    RopUI::Tests::run("hive_epoll.post_and_post_delayed", test_post_and_post_delayed);
    RopUI::Tests::run("hive_epoll.post_to_worker_private_queue", test_post_to_worker_private_queue);
    RopUI::Tests::run("hive_compute_pool.runs_tasks", test_compute_pool_runs_tasks);
    RopUI::Tests::run("hive_poll.io_backend_runs", test_io_poll_backend_runs);
    return 0;
}
