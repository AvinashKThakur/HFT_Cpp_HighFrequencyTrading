#pragma once
#include "Types.h"
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <algorithm>

namespace HFT {

// -----------------------------------------------------------------------
// Design notes:
//
// A naive book stores resting orders in a std::vector<Order> per price
// level. NEW is O(1) amortized (push_back), but CANCEL and MODIFY are
// O(n) per level, because finding a specific order_id means a linear
// scan. In a live market, cancels/modifies vastly outnumber new orders
// (order-to-trade ratios of 50:1-100:1 are typical), so an O(n) cancel
// path is optimizing the wrong thing.
//
// This version fixes that with:
//   - A fixed pool of nodes (no heap allocation once warmed up).
//   - An intrusive doubly-linked list per price level for FIFO
//     (price-time) priority.
//   - Each node stores which (side, price level) list it belongs to,
//     so unlinking it — for a cancel, a modify-to-zero, or a fill —
//     is a single O(1) operation that can correctly fix up that list's
//     head/tail pointers.
//   - A hash index (order_id -> pool slot) for O(1) lookup.
//
// Trade-off worth naming in an interview: an increase-in-size MODIFY
// here keeps the order's existing time-priority slot rather than
// re-queueing it to the back of the level. Most real venues actually
// revoke time priority on a size increase (not on a decrease) — that's
// a one-line policy choice you'd make differently depending on the
// exchange you're modeling.
// -----------------------------------------------------------------------

class OrderBook {
private:
    static constexpr size_t MAX_PRICE_LEVELS = 10000;
    static constexpr size_t MAX_ORDERS = 1 << 20; // 1,048,576 resting orders

    struct Node {
        uint32_t order_id{0};
        uint32_t qty{0};
        int32_t prev{-1};
        int32_t next{-1};
        uint32_t level_idx{0};
        Side side{Side::BUY};
    };

    struct PriceLevel {
        int32_t head{-1};
        int32_t tail{-1};
    };

    std::vector<Node> pool_;
    std::vector<int32_t> free_list_;
    size_t free_top_{0};

    std::vector<PriceLevel> levels_buy_;
    std::vector<PriceLevel> levels_sell_;

    std::unordered_map<uint32_t, int32_t> index_; // order_id -> pool slot

public:
    OrderBook() {
        pool_.resize(MAX_ORDERS);
        free_list_.resize(MAX_ORDERS);
        for (size_t i = 0; i < MAX_ORDERS; ++i) free_list_[i] = static_cast<int32_t>(i);
        free_top_ = MAX_ORDERS;

        levels_buy_.assign(MAX_PRICE_LEVELS, PriceLevel{});
        levels_sell_.assign(MAX_PRICE_LEVELS, PriceLevel{});

        index_.reserve(MAX_ORDERS);
    }

    void handle_order(const MarketUpdate& update) {
        if (update.price_scaled >= MAX_PRICE_LEVELS) return;

        switch (update.action) {
            case Action::NEW:
                match_or_add(update);
                break;
            case Action::CANCEL:
                cancel_order(update.order_id);
                break;
            case Action::MODIFY:
                modify_order(update.order_id, update.quantity);
                break;
        }
    }

    // O(1): index lookup + intrusive unlink (fixes head/tail of its own
    // level) + return node to the free pool.
    bool cancel_order(uint32_t order_id) {
        auto it = index_.find(order_id);
        if (it == index_.end()) return false;
        int32_t idx = it->second;
        unlink(idx);
        index_.erase(it);
        free_node(idx);
        return true;
    }

    // O(1): index lookup + in-place quantity update. See design note
    // above re: time-priority-on-increase, which this simplified
    // version does not model.
    bool modify_order(uint32_t order_id, uint32_t new_qty) {
        auto it = index_.find(order_id);
        if (it == index_.end()) return false;
        pool_[it->second].qty = new_qty;
        return true;
    }

    size_t resting_order_count() const { return index_.size(); }

private:
    PriceLevel& level_for(const Node& n) {
        auto& levels = (n.side == Side::BUY) ? levels_buy_ : levels_sell_;
        return levels[n.level_idx];
    }

    int32_t alloc_node() {
        if (free_top_ == 0) return -1; // pool exhausted
        return free_list_[--free_top_];
    }

    void free_node(int32_t idx) {
        pool_[idx] = Node{};
        free_list_[free_top_++] = idx;
    }

    void push_back(PriceLevel& level, int32_t idx) {
        pool_[idx].prev = level.tail;
        pool_[idx].next = -1;
        if (level.tail != -1) pool_[level.tail].next = idx;
        level.tail = idx;
        if (level.head == -1) level.head = idx;
    }

    // Single, correct O(1) unlink: uses the node's own (side, level_idx)
    // to find and fix up the owning list's head/tail.
    void unlink(int32_t idx) {
        Node& n = pool_[idx];
        PriceLevel& level = level_for(n);
        if (n.prev != -1) pool_[n.prev].next = n.next; else level.head = n.next;
        if (n.next != -1) pool_[n.next].prev = n.prev; else level.tail = n.prev;
    }

    void match_or_add(const MarketUpdate& update) {
        auto& same_side_levels = (update.order_side == Side::BUY) ? levels_buy_ : levels_sell_;
        auto& opp_levels = (update.order_side == Side::BUY) ? levels_sell_ : levels_buy_;

        uint32_t remaining_qty = update.quantity;
        PriceLevel& opposite = opp_levels[update.price_scaled];

        int32_t cur = opposite.head;
        while (cur != -1 && remaining_qty > 0) {
            int32_t next = pool_[cur].next;
            uint32_t match_qty = std::min(remaining_qty, pool_[cur].qty);
            pool_[cur].qty -= match_qty;
            remaining_qty -= match_qty;
            if (pool_[cur].qty == 0) {
                uint32_t filled_id = pool_[cur].order_id;
                unlink(cur);
                index_.erase(filled_id);
                free_node(cur);
            }
            cur = next;
        }

        if (remaining_qty > 0) {
            int32_t idx = alloc_node();
            if (idx == -1) return; // pool exhausted; bounded memory, drop resting order
            pool_[idx].order_id = update.order_id;
            pool_[idx].qty = remaining_qty;
            pool_[idx].side = update.order_side;
            pool_[idx].level_idx = update.price_scaled;
            push_back(same_side_levels[update.price_scaled], idx);
            index_[update.order_id] = idx;
        }
    }
};

} // namespace HFT