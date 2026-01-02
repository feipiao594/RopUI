#include "platform/schedule/hive.h"
#include "platform/schedule/compute_worker.h"
#include "platform/schedule/io_worker.h"

#include <atomic>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int main() {
    RopHive::Hive hive;
    hive.attachIOWorker(std::make_shared<RopHive::IOWorker>(hive.options()));
    hive.attachComputeWorker(std::make_shared<RopHive::ComputeWorker>(hive.options()));

    std::atomic<int> counter{0};

    std::thread runner([&] { hive.run(); });

    hive.postIO([&] {
        counter.fetch_add(1, std::memory_order_relaxed);
        std::cout << "micro task ran\n";
    });

    hive.postDelayed([&] {
        counter.fetch_add(10, std::memory_order_relaxed);
        std::cout << "delayed task ran, requesting exit\n";
        hive.requestExit();
    }, 50ms);

    hive.postCompute([&] {
        counter.fetch_add(100, std::memory_order_relaxed);
        std::cout << "compute task ran\n";
    });

    runner.join();

    std::cout << "done, counter=" << counter.load(std::memory_order_relaxed) << "\n";
    return 0;
}
