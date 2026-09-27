#include "../include/OrderBook.h"
#include <cassert>
#include <iostream>

using namespace HFT;

static MarketUpdate make(Action a, Side s, uint32_t id, uint32_t px, uint32_t qty) {
    MarketUpdate m{};
    m.action = a;
    m.order_side = s;
    m.order_id = id;
    m.price_scaled = px;
    m.quantity = qty;
    return m;
}

int main() {
    OrderBook book;

    // 1. Add a resting buy order, confirm it's tracked.
    book.handle_order(make(Action::NEW, Side::BUY, 1, 100, 50));
    assert(book.resting_order_count() == 1);

    // 2. Cancel it, confirm O(1) removal actually removes it.
    bool cancelled = book.cancel_order(1);
    assert(cancelled);
    assert(book.resting_order_count() == 0);
    // Cancelling again should fail cleanly (already gone).
    assert(book.cancel_order(1) == false);

    // 3. Add two resting sell orders at the same price (FIFO priority).
    book.handle_order(make(Action::NEW, Side::SELL, 2, 200, 30));
    book.handle_order(make(Action::NEW, Side::SELL, 3, 200, 30));
    assert(book.resting_order_count() == 2);

    // 4. Modify order 2's quantity down to 10.
    bool modified = book.modify_order(2, 10);
    assert(modified);

    // 5. An incoming aggressive buy for 15 at price 200 should:
    //    - fully consume order 2 (qty 10, FIFO-first) and remove it
    //    - partially consume order 3 for the remaining 5
    book.handle_order(make(Action::NEW, Side::BUY, 4, 200, 15));
    assert(book.resting_order_count() == 1); // only order 3 remains, partially filled

    // 6. Cancel the remaining order 3, book should be empty.
    assert(book.cancel_order(3));
    assert(book.resting_order_count() == 0);

    // 7. Stress: add + cancel 100,000 orders across many price levels,
    //    confirm bookkeeping stays consistent (no leaks, no double-frees).
    for (uint32_t i = 0; i < 100000; ++i) {
        uint32_t px = i % 5000;
        book.handle_order(make(Action::NEW, Side::BUY, 1000 + i, px, 1));
    }
    assert(book.resting_order_count() == 100000);
    for (uint32_t i = 0; i < 100000; ++i) {
        assert(book.cancel_order(1000 + i));
    }
    assert(book.resting_order_count() == 0);

    std::cout << "All OrderBook correctness tests passed.\n";
    return 0;
}