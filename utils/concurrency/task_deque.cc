#include "concurrency/task_deque.h"

namespace RopUI::Utils::Concurrency {

static bool isPow2(size_t x) noexcept {
    return x != 0 && (x & (x - 1)) == 0;
}

size_t TaskDeque::roundUpPow2(size_t x) {
    if (x < 2) {
        return 2;
    }
    if (isPow2(x)) {
        return x;
    }
    size_t p = 1;
    while (p < x) {
        p <<= 1;
    }
    return p;
}

TaskDeque::TaskDeque(size_t capacity)
    : capacity_(roundUpPow2(capacity)), buffer_(capacity_) {}

size_t TaskDeque::capacity() const noexcept {
    return capacity_;
}

size_t TaskDeque::maxSize() const noexcept {
    return capacity_ > 0 ? (capacity_ - 1) : 0;
}

size_t TaskDeque::approxSize() const noexcept {
    return size_.load(std::memory_order_acquire);
}

size_t TaskDeque::remainingSpace() const noexcept {
    const size_t b = bottom_.load(std::memory_order_acquire);
    const size_t t = top_.load(std::memory_order_acquire);
    const size_t used = b >= t ? (b - t) : 0;
    const size_t max_sz = maxSize();
    return used >= max_sz ? 0 : (max_sz - used);
}

bool TaskDeque::tryPushBottom(std::function<void()> task) {
    if (!task) {
        return true;
    }
    const size_t b = bottom_.load(std::memory_order_relaxed);
    const size_t t = top_.load(std::memory_order_acquire);
    if (b - t >= maxSize()) {
        return false;
    }
    buffer_[b & (capacity_ - 1)] = std::move(task);
    std::atomic_thread_fence(std::memory_order_release);
    bottom_.store(b + 1, std::memory_order_release);
    size_.fetch_add(1, std::memory_order_release);
    return true;
}

std::optional<std::function<void()>> TaskDeque::tryPopBottom() {
    size_t b = bottom_.load(std::memory_order_relaxed);
    if (b == 0) {
        return std::nullopt;
    }
    b -= 1;
    bottom_.store(b, std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_seq_cst);
    size_t t = top_.load(std::memory_order_relaxed);

    if (t <= b) {
        if (t == b) {
            size_t expected = t;
            if (!top_.compare_exchange_strong(
                    expected, t + 1,
                    std::memory_order_seq_cst,
                    std::memory_order_relaxed)) {
                bottom_.store(b + 1, std::memory_order_relaxed);
                return std::nullopt;
            }
            bottom_.store(b + 1, std::memory_order_relaxed);
            size_.fetch_sub(1, std::memory_order_release);
            return std::move(buffer_[b & (capacity_ - 1)]);
        }
        std::function<void()> item = std::move(buffer_[b & (capacity_ - 1)]);
        size_.fetch_sub(1, std::memory_order_release);
        return item;
    }

    bottom_.store(b + 1, std::memory_order_relaxed);
    return std::nullopt;
}

std::optional<std::function<void()>> TaskDeque::tryStealTop() {
    size_t t = top_.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    const size_t b = bottom_.load(std::memory_order_acquire);

    if (t < b) {
        size_t expected = t;
        if (top_.compare_exchange_strong(
                expected, t + 1,
                std::memory_order_seq_cst,
                std::memory_order_relaxed)) {
            size_.fetch_sub(1, std::memory_order_release);
            return std::move(buffer_[t & (capacity_ - 1)]);
        }
    }
    return std::nullopt;
}

} // namespace RopUI::Utils::Concurrency
