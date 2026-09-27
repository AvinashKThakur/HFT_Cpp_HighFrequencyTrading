// Real latency benchmark for OrderBook::handle_order (NEW / CANCEL / MODIFY).
//
// This measures actual wall-clock latency per operation using
// std::chrono::steady_clock, not an estimated or "simulated" figure.
// Numbers will vary by machine -- run this on whatever box you want to
// quote numbers for, don't copy the numbers from this run verbatim.
//
// Build:  g++ -std=c++17 -O3 -march=native src/benchmark.cpp -o benchmark
// Run:    ./benchmark

#include "../include/OrderBook.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace HFT;
using Clock = std::chrono::steady_clock;

struct Stats {
    double p50, p90, p99, p999, mean;
};

static Stats summarize(std::vector<double>& latencies_ns) {
    std::sort(latencies_ns.begin(), latencies_ns.end());
    auto pct = [&](double p) {
        size_t idx = static_cast<size_t>(p * (latencies_ns.size() - 1));
        return latencies_ns[idx];
    };
    double sum = 0;
    for (double v : latencies_ns) sum += v;
    return Stats{pct(0.50), pct(0.90), pct(0.99), pct(0.999), sum / latencies_ns.size()};
}

static void print_stats(const char* label, const Stats& s) {
    std::printf("%-24s  mean=%7.1f ns  p50=%7.1f ns  p90=%7.1f ns  p99=%7.1f ns  p99.9=%8.1f ns\n",
                label, s.mean, s.p50, s.p90, s.p99, s.p999);
}

int main() {
    constexpr size_t N = 500000;
    OrderBook book;

    std::mt19937 rng(42);
    std::uniform_int_distribution<uint32_t> price_dist(0, 9999);

    // --- Benchmark NEW (add resting order) ---
    std::vector<double> add_latencies;
    add_latencies.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        MarketUpdate m{};
        m.action = Action::NEW;
        m.order_side = (i % 2 == 0) ? Side::BUY : Side::SELL;
        m.order_id = static_cast<uint32_t>(i);
        // Keep buys and sells on non-overlapping price ranges so this
        // pass measures pure "add to book", not matching/removal.
        m.price_scaled = (i % 2 == 0) ? price_dist(rng) % 4000 : 6000 + (price_dist(rng) % 4000);
        m.quantity = 10;

        auto t0 = Clock::now();
        book.handle_order(m);
        auto t1 = Clock::now();
        add_latencies.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    print_stats("NEW (add resting order)", summarize(add_latencies));
    std::printf("  resting after adds: %zu\n\n", book.resting_order_count());

    // --- Benchmark MODIFY (in-place qty change on existing resting orders) ---
    std::vector<double> modify_latencies;
    modify_latencies.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        auto t0 = Clock::now();
        book.modify_order(static_cast<uint32_t>(i), 5);
        auto t1 = Clock::now();
        modify_latencies.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    print_stats("MODIFY (qty update)", summarize(modify_latencies));
    std::printf("\n");

    // --- Benchmark CANCEL (O(1) index lookup + intrusive unlink) ---
    std::vector<double> cancel_latencies;
    cancel_latencies.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        auto t0 = Clock::now();
        book.cancel_order(static_cast<uint32_t>(i));
        auto t1 = Clock::now();
        cancel_latencies.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    print_stats("CANCEL (O(1))", summarize(cancel_latencies));
    std::printf("  resting after cancels: %zu (should be 0)\n", book.resting_order_count());

    return 0;
}