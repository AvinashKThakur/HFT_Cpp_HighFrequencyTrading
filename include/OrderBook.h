#pragma once
#include "Types.h"
#include <vector>
#include <algorithm>
#include <iostream>

namespace HFT {

struct Order {
    uint32_t id;
    uint32_t qty;
};

class OrderBook {
private:
    static constexpr size_t MAX_PRICE_LEVELS = 10000;
    
    // Contiguous arrays for O(1) indexing: Index = Price_Scaled
    std::vector<Order> buy_levels_[MAX_PRICE_LEVELS];
    std::vector<Order> sell_levels_[MAX_PRICE_LEVELS];

public:
    OrderBook() {
        // Reserve internal capacity upfront during initialization to prevent runtime allocations
        for (size_t i = 0; i < MAX_PRICE_LEVELS; ++i) {
            buy_levels_[i].reserve(64);
            sell_levels_[i].reserve(64);
        }
    }

    void handle_order(const MarketUpdate& update) {
        if (update.price_scaled >= MAX_PRICE_LEVELS) return;

        if (update.order_side == Side::BUY) {
            match_or_add(update, buy_levels_[update.price_scaled], sell_levels_);
        } else {
            match_or_add(update, sell_levels_[update.price_scaled], buy_levels_);
        }
    }

private:
    void match_or_add(const MarketUpdate& update, std::vector<Order>& same_side, std::vector<Order>* opposite_side_matrix) {
        uint32_t remaining_qty = update.quantity;
        auto& opposite_side = opposite_side_matrix[update.price_scaled];

        // 1. Cross/Match orders on identical price level (Aggressive Order matching)
        for (auto& resting_order : opposite_side) {
            if (resting_order.qty == 0) continue;

            uint32_t match_qty = std::min(remaining_qty, resting_order.qty);
            resting_order.qty -= match_qty;
            remaining_qty -= match_qty;

            if (remaining_qty == 0) break;
        }

        // Clean up empty processed orders
        opposite_side.erase(std::remove_if(opposite_side.begin(), opposite_side.end(), 
            [](const Order& o) { return o.qty == 0; }), opposite_side.end());

        // 2. If quantity remains, insert it into the book as a resting limit order
        if (remaining_qty > 0) {
            same_side.push_back(Order{update.order_id, remaining_qty});
        }
    }
};

} // namespace HFT
