#include "test_util.h"

#include "concurrency/task_deque.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

using RopUI::Utils::Concurrency::TaskDeque;

static void test_basic_push_pop() {
    TaskDeque dq(8);
    ROPUI_TEST_ASSERT(dq.maxSize() == 7);
    ROPUI_TEST_ASSERT(dq.remainingSpace() == 7);
    ROPUI_TEST_ASSERT(dq.approxSize() == 0);

    std::vector<int> order;
    order.reserve(3);

    ROPUI_TEST_ASSERT(dq.tryPushBottom([&] { order.push_back(1); }));
    ROPUI_TEST_ASSERT(dq.tryPushBottom([&] { order.push_back(2); }));
    ROPUI_TEST_ASSERT(dq.tryPushBottom([&] { order.push_back(3); }));
    ROPUI_TEST_ASSERT(dq.approxSize() == 3);
    ROPUI_TEST_ASSERT(dq.remainingSpace() == 4);

    auto a = dq.tryPopBottom();
    auto b = dq.tryPopBottom();
    auto c = dq.tryPopBottom();
    auto d = dq.tryPopBottom();

    ROPUI_TEST_ASSERT(a.has_value());
    ROPUI_TEST_ASSERT(b.has_value());
    ROPUI_TEST_ASSERT(c.has_value());
    ROPUI_TEST_ASSERT(!d.has_value());

    (*a)();
    (*b)();
    (*c)();

    ROPUI_TEST_ASSERT(order.size() == 3);
    ROPUI_TEST_ASSERT(order[0] == 3);
    ROPUI_TEST_ASSERT(order[1] == 2);
    ROPUI_TEST_ASSERT(order[2] == 1);
    ROPUI_TEST_ASSERT(dq.approxSize() == 0);
    ROPUI_TEST_ASSERT(dq.remainingSpace() == dq.maxSize());
}

static void test_batch_push_cursor_and_pop_batch_order() {
    TaskDeque dq(8);
    std::vector<int> order;
    order.reserve(7);

    std::vector<std::function<void()>> tasks;
    tasks.reserve(7);
    for (int i = 1; i <= 7; ++i) {
        tasks.emplace_back([&, i] { order.push_back(i); });
    }
    for (auto& t : tasks) {
        ROPUI_TEST_ASSERT(dq.tryPushBottom(std::move(t)));
    }

    ROPUI_TEST_ASSERT(dq.remainingSpace() == 0);
    ROPUI_TEST_ASSERT(!dq.tryPushBottom([&] { order.push_back(999); }));

    // bottom pop is LIFO: last pushed runs first
    for (int i = 0; i < 4; ++i) {
        auto opt = dq.tryPopBottom();
        ROPUI_TEST_ASSERT(opt.has_value());
        (*opt)();
    }

    ROPUI_TEST_ASSERT(order.size() == 4);
    ROPUI_TEST_ASSERT(order[0] == 7);
    ROPUI_TEST_ASSERT(order[1] == 6);
    ROPUI_TEST_ASSERT(order[2] == 5);
    ROPUI_TEST_ASSERT(order[3] == 4);
    ROPUI_TEST_ASSERT(dq.approxSize() == 3);
    ROPUI_TEST_ASSERT(dq.remainingSpace() == 4);
}

static void test_steal_batch_order_is_from_top() {
    TaskDeque dq(16);

    std::vector<int> order;
    order.reserve(8);

    for (int i = 1; i <= 8; ++i) {
        ROPUI_TEST_ASSERT(dq.tryPushBottom([&, i] { order.push_back(i); }));
    }

    for (int i = 0; i < 3; ++i) {
        auto opt = dq.tryStealTop();
        ROPUI_TEST_ASSERT(opt.has_value());
        (*opt)();
    }

    // top steal is FIFO from oldest tasks: 1,2,3
    ROPUI_TEST_ASSERT(order.size() == 3);
    ROPUI_TEST_ASSERT(order[0] == 1);
    ROPUI_TEST_ASSERT(order[1] == 2);
    ROPUI_TEST_ASSERT(order[2] == 3);
    ROPUI_TEST_ASSERT(dq.approxSize() == 5);
}

static void test_steal_batch_no_loss() {
    constexpr size_t N = 20000;
    TaskDeque dq(32768);

    auto hits = std::make_unique<std::atomic<int>[]>(N);
    for (size_t i = 0; i < N; ++i) {
        hits[i].store(0, std::memory_order_relaxed);
    }

    std::atomic<size_t> done{0};

    std::vector<std::function<void()>> tasks;
    tasks.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        tasks.emplace_back([&, i] {
            hits[i].fetch_add(1, std::memory_order_relaxed);
            done.fetch_add(1, std::memory_order_relaxed);
        });
    }

    for (auto& f : tasks) {
        while (!dq.tryPushBottom(std::move(f))) {
            std::this_thread::yield();
        }
    }
    ROPUI_TEST_ASSERT(dq.approxSize() == N);

    std::atomic<bool> stop{false};

    auto worker_owner = [&] {
        while (!stop.load(std::memory_order_relaxed)) {
            if (done.load(std::memory_order_relaxed) >= N) {
                break;
            }
            auto opt = dq.tryPopBottom();
            if (!opt.has_value()) {
                std::this_thread::yield();
                continue;
            }
            (*opt)();
        }
    };

    auto worker_thief = [&] {
        while (!stop.load(std::memory_order_relaxed)) {
            if (done.load(std::memory_order_relaxed) >= N) {
                break;
            }
            auto opt = dq.tryStealTop();
            if (!opt.has_value()) {
                std::this_thread::yield();
                continue;
            }
            (*opt)();
        }
    };

    std::thread owner(worker_owner);
    std::thread thief1(worker_thief);
    std::thread thief2(worker_thief);
    std::thread thief3(worker_thief);

    while (done.load(std::memory_order_relaxed) < N) {
        std::this_thread::yield();
    }
    stop.store(true, std::memory_order_relaxed);

    owner.join();
    thief1.join();
    thief2.join();
    thief3.join();

    ROPUI_TEST_ASSERT(dq.approxSize() == 0);
    ROPUI_TEST_ASSERT(dq.remainingSpace() == dq.maxSize());

    for (size_t i = 0; i < N; ++i) {
        const int v = hits[i].load(std::memory_order_relaxed);
        ROPUI_TEST_ASSERT(v == 1);
    }
}

int main() {
    RopUI::Tests::run("task_deque.basic_push_pop", test_basic_push_pop);
    RopUI::Tests::run("task_deque.batch_push_pop", test_batch_push_cursor_and_pop_batch_order);
    RopUI::Tests::run("task_deque.steal_batch_order", test_steal_batch_order_is_from_top);
    RopUI::Tests::run("task_deque.concurrent_no_loss", test_steal_batch_no_loss);
    return 0;
}
