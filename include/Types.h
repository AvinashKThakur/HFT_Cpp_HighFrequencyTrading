#pragma once
#include <cstdint>
#include <cstddef>
#include <new>

namespace HFT {

// Strict structural protocol definitions
enum class Side : char { BUY = 'B', SELL = 'S' };

// What kind of instruction this packet represents.
// Real venues (ITCH/OUCH/FIX) distinguish these explicitly; a book that
// only ever adds orders can't reflect real trader behavior, where the
// majority of message volume is cancels and modifies, not new orders.
enum class Action : char { NEW = 'N', CANCEL = 'C', MODIFY = 'M' };

// Fixed-width representation to match zero-copy parser layout
struct MarketUpdate {
    uint64_t timestamp_ns;
    uint32_t price_scaled;   // Price * 10,000 (Eliminates float instability)
    uint32_t quantity;       // For MODIFY: the new quantity
    uint32_t order_id;
    Side order_side;
    Action action{Action::NEW};
    char symbol[4];          // Inline array padding to avoid dynamic heap strings
};

// Most x86_64/ARM64 targets used in production HFT boxes have 64-byte
// cache lines. std::hardware_destructive_interference_size exists for
// this, but GCC/Clang warn that its value can silently change between
// compiler versions/tuning flags, which is exactly the kind of subtle
// portability bug you don't want in a hot path -- pin it explicitly and
// verify with `getconf LEVEL1_DCACHE_LINESIZE` on the target box.
inline constexpr size_t CACHE_LINE_SIZE = 64;

// Align to separate cache line blocks to isolate thread mutations completely
struct alignas(CACHE_LINE_SIZE) ThreadData {
    uint64_t processed_packets{0};
};

} // namespace HFT