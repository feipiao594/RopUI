#ifndef _ROPUI_UTILS_CONCURRENCY_TASKDEQUE_H
#define _ROPUI_UTILS_CONCURRENCY_TASKDEQUE_H

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace RopUI::Utils::Concurrency {

class TaskDeque {
public:
    explicit TaskDeque(size_t capacity);

    TaskDeque(const TaskDeque&) = delete;
    TaskDeque& operator=(const TaskDeque&) = delete;

    size_t capacity() const noexcept;
    size_t maxSize() const noexcept;
    size_t approxSize() const noexcept;
    size_t remainingSpace() const noexcept;

    bool tryPushBottom(const std::function<void()>& task);
    bool tryPushBottom(std::function<void()>&& task);
    std::optional<std::function<void()>> tryPopBottom();
    std::optional<std::function<void()>> tryStealTop();

private:
    static size_t roundUpPow2(size_t x);

private:
    const size_t capacity_;
    std::vector<std::function<void()>> buffer_;
    alignas(64) std::atomic<size_t> top_{0};
    alignas(64) std::atomic<size_t> bottom_{0};
    alignas(64) std::atomic<size_t> size_{0};
};

} 

#endif // _ROPUI_UTILS_CONCURRENCY_TASKDEQUE_H
