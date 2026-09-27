# LightSpeed-L3-Matcher 🚀

An ultra-low-latency L3 limit order book matching engine and zero-copy
ingestion pipeline sandbox, written in modern C++17.

## What this demonstrates

- **Lock-free queue isolation** — a single-producer/single-consumer ring
  buffer (`include/SPSCQueue.h`) using explicit `memory_order_release` /
  `memory_order_acquire` atomics instead of locks.
- **O(1) order management** — `include/OrderBook.h` uses a pooled,
  intrusive doubly-linked list per price level plus a hash index, so
  add, cancel, and modify are all O(1). (An earlier, simpler version of
  this used `std::vector` per price level, which made cancel/modify
  O(n) — see "Design decisions" below for why that mattered enough to
  change.)
- **False-sharing mitigation** — hot atomics are padded to their own
  cache line (`alignas(CACHE_LINE_SIZE)`), pinned to 64 bytes explicitly
  rather than relying on `std::hardware_destructive_interference_size`,
  which the standard allows to vary by compiler/tuning flags.
- **Zero steady-state allocation** — the order book pre-allocates a
  fixed pool of order-nodes and price levels at startup; adding,
  cancelling, and modifying orders during the run touch no heap.
- **Core affinity** — producer and consumer threads are pinned to
  separate CPU cores via `pthread_setaffinity_np` to avoid
  kernel-rescheduling jitter.

## Build & run

```bash
# Option 1: Makefile
make run-all          # builds main, test_orderbook, benchmark; runs all three
make test             # just the correctness suite
make benchmark        # just the latency benchmark

# Option 2: CMake
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make
ctest                 # runs the correctness suite
./benchmark
./main
```

## Correctness

`src/test_orderbook.cpp` is a standalone assertion-based test suite
covering add/cancel/modify, FIFO matching at a price level, double-cancel
safety, and a 100K-order add/cancel stress pass to catch leaks or
double-frees in the pool allocator. Run via `make test` or `ctest`.

## Latency (measured, not simulated)

`src/benchmark.cpp` times 500,000 NEW / MODIFY / CANCEL operations with
`std::chrono::steady_clock` and reports the actual mean/p50/p90/p99/p99.9
observed on the run — nothing here is an estimate. Numbers are hardware-
and load-dependent; run `make benchmark` on your own machine before
quoting a number anywhere.

Example run (Ubuntu 24.04, GCC 13, `-O3 -march=native`, otherwise idle box):

| Operation | mean | p50 | p90 | p99 | p99.9 |
|---|---|---|---|---|---|
| NEW (add resting order) | 71.1 ns | 51.0 ns | 55.0 ns | 256.0 ns | 1801.0 ns |
| MODIFY (qty update) | 40.4 ns | 39.0 ns | 39.0 ns | 48.0 ns | 140.0 ns |
| CANCEL (O(1)) | 68.1 ns | 52.0 ns | 112.0 ns | 221.0 ns | 318.0 ns |

The p50→p99.9 gap is worth noting rather than hiding: it's consistent
with first-touch page faults on the pre-allocated node pool (a 1M-entry
`std::vector<Node>` is reserved but not memory-resident until written).
Production systems avoid this by warm-touching all pool memory before
going live, rather than relying on the allocator/OS to fault it in
during the hot path.

## Design decisions & known trade-offs

- **Why an intrusive linked list instead of `std::vector` per price
  level:** a vector makes NEW O(1) amortized but CANCEL/MODIFY O(n),
  since finding a specific `order_id` means scanning the vector. Real
  order-to-trade ratios are dominated by cancels/modifies (commonly
  50:1–100:1 vs. new orders), so optimizing NEW at the expense of
  CANCEL is optimizing the wrong operation.
- **Time priority on MODIFY:** this book keeps an order's original
  queue position when its size is increased. Most real venues instead
  revoke time priority on a size increase (a decrease is "free"). This
  is a one-line policy choice that would differ by venue being modeled.
- **Single symbol, single price-time-priority book:** no cross-symbol
  routing, no iceberg/hidden-order support, no FIX/ITCH-style binary
  wire parsing — `MarketUpdate` is a synthetic in-memory struct, not a
  parsed wire packet.
- **Bounded memory:** the order pool has a fixed capacity
  (`MAX_ORDERS = 2^20`); if exhausted, new resting orders are dropped
  rather than the pool growing, which is a deliberate choice to keep
  worst-case latency bounded (no allocation ever happens on the hot
  path) at the cost of a hard ceiling on open orders.

## Repo layout

```
include/
  Types.h        # MarketUpdate wire struct, Side/Action enums
  SPSCQueue.h    # lock-free single-producer/single-consumer ring buffer
  OrderBook.h    # O(1) add/cancel/modify limit order book
src/
  main.cpp           # producer/consumer demo wiring the two together
  test_orderbook.cpp # correctness tests for OrderBook
  benchmark.cpp      # latency measurement harness
Makefile
CMakeLists.txt
```