#pragma once
#include <atomic>
#include <new>
#include <vector>
#include "Types.h"

namespace HFT {

template<typename T, size_t Capacity>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2 for fast masking.");
private:
    // Isolate read/write heads to separate cache lines to completely prevent false sharing
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
    T buffer_[Capacity];

public:
    SPSCQueue() = default;
    ~SPSCQueue() = default;

    // Disallow copies for strict single-instance pipeline boundaries
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    template<typename... Args>
    bool emplace(Args&&... args) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t current_head = head_.load(std::memory_order_acquire); // Pull down latest consumer head
        if ((current_tail - current_head) == Capacity) {
            return false; // Queue full
        }
        buffer_[current_tail & (Capacity - 1)] = T(std::forward<Args>(args)...);
        tail_.store(current_tail + 1, std::memory_order_release); // Release writes to global memory visibility
        return true;
    }

    bool pop(T& value) {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_acquire); // Pull down latest producer tail
        if (current_head == current_tail) {
            return false; // Queue empty
        }
        value = buffer_[current_head & (Capacity - 1)];
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }
};

} // namespace HFT