#pragma once
#include <cstdint>
#include <new>

namespace HFT {

// Strict structural protocol definitions
enum class Side : char { BUY = 'B', SELL = 'S' };

// Fixed-width representation to match zero-copy parser layout
struct MarketUpdate {
    uint64_t timestamp_ns;
    uint32_t price_scaled;  // Price * 10,000 (Eliminates float instability)
    uint32_t quantity;
    uint32_t order_id;
    Side order_side;
    char symbol[4];         // Inline array padding to avoid dynamic heap strings
};

// Align to separate cache line blocks to isolate thread mutations completely
struct alignas(std::hardware_destructive_interference_size) ThreadData {
    uint64_t processed_packets{0};
};

} // namespace HFT
