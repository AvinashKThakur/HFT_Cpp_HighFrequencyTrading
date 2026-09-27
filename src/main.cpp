#include "../include/SPSCQueue.h"
#include "../include/OrderBook.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <pthread.h>

void pin_thread_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t current_thread = pthread_self();
    pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

int main() {
    constexpr size_t QUEUE_CAPACITY = 1024;
    auto ring_buffer = std::make_unique<HFT::SPSCQueue<HFT::MarketUpdate, QUEUE_CAPACITY>>();
    std::atomic<bool> system_running{true};

    // Thread 1: Market Data Network Parser Simulator
    std::thread producer([&]() {
        pin_thread_to_core(1); // Pin explicitly to Core 1
        
        uint32_t order_counter = 1;
        for (int i = 0; i < 500000; ++i) {
            HFT::MarketUpdate packet;
            packet.timestamp_ns = std::chrono::high_resolution_clock::now().time_since_epoch().count();
            packet.price_scaled = 500 + (i % 10); // Simulated price data
            packet.quantity = 10;
            packet.order_id = order_counter++;
            packet.order_side = (i % 2 == 0) ? HFT::Side::BUY : HFT::Side::SELL;
            packet.symbol[0] = 'A'; packet.symbol[1] = 'P'; packet.symbol[2] = 'P'; packet.symbol[3] = '\0';

            // Spin-wait (busy-poll) if the queue is temporarily full
            while (!ring_buffer->emplace(packet)) {
                std::this_thread::yield();
            }
        }
        system_running.store(false);
    });

    // Thread 2: Sub-Microsecond Core Matching Engine
    std::thread consumer([&]() {
        pin_thread_to_core(2); // Pin explicitly to Core 2
        HFT::OrderBook matching_engine;
        HFT::MarketUpdate current_job;
        uint64_t processed_count = 0;

        while (system_running.load(std::memory_order_relaxed) || ring_buffer->pop(current_job)) {
            if (ring_buffer->pop(current_job)) {
                matching_engine.handle_order(current_job);
                processed_count++;
            }
        }
        std::cout << "Engine execution loop finalized. Processed Orders: " << processed_count << "\n";
    });

    producer.join();
    consumer.join();
    return 0;
}
