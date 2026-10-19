#include <cstdio>

#include "me/reference_book.hpp"

using namespace me;

static int failures = 0;

// Variadic so braced lists with commas (Events{Rejected{1, ...}}) can be passed without extra parens.
#define CHECK(...)                                                                            \
    do {                                                                                      \
        if (!(__VA_ARGS__)) {                                                                 \
            std::fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #__VA_ARGS__); \
            ++failures;                                                                       \
        }                                                                                     \
    } while (0)

constexpr Side Buy  = Side::Buy;
constexpr Side Sell = Side::Sell;

// --- resting ---

static void limit_buy_on_empty_book_rests() {
    ReferenceBook b;
    CHECK(b.limit(1, Buy, 100, 10) == Events{Ack{1}});
    CHECK(b.best_bid() == 100);
    CHECK(!b.best_ask());
    CHECK(b.depth(Buy, 100) == 10);
}

static void limit_sell_on_empty_book_rests() {
    ReferenceBook b;
    CHECK(b.limit(1, Sell, 100, 10) == Events{Ack{1}});
    CHECK(b.best_ask() == 100);
    CHECK(!b.best_bid());
}

static void non_crossing_orders_both_rest() {
    ReferenceBook b;
    b.limit(1, Buy, 99, 5);
    CHECK(b.limit(2, Sell, 101, 5) == Events{Ack{2}});
    CHECK(b.best_bid() == 99);
    CHECK(b.best_ask() == 101);
    CHECK(b.order_count() == 2);
}

static void orders_at_same_price_add_to_level() {
    ReferenceBook b;
    b.limit(1, Buy, 100, 3);
    b.limit(2, Buy, 100, 4);
    CHECK(b.depth(Buy, 100) == 7);
}

// --- matching ---

static void crossing_buy_trades_at_maker_price() {
    ReferenceBook b;
    b.limit(1, Sell, 100, 10);
    CHECK(b.limit(2, Buy, 105, 10) == (Events{Ack{2}, Trade{2, 1, Buy, 100, 10}}));
    CHECK(b.order_count() == 0);
    CHECK(!b.best_bid() && !b.best_ask());
}

static void crossing_sell_hits_highest_bid_first() {
    ReferenceBook b;
    b.limit(1, Buy, 99, 5);
    b.limit(2, Buy, 101, 5);
    CHECK(b.limit(3, Sell, 99, 5) == (Events{Ack{3}, Trade{3, 2, Sell, 101, 5}}));
    CHECK(b.best_bid() == 99);
}

static void taker_remainder_rests_at_its_limit() {
    ReferenceBook b;
    b.limit(1, Sell, 100, 4);
    CHECK(b.limit(2, Buy, 102, 10) == (Events{Ack{2}, Trade{2, 1, Buy, 100, 4}}));
    CHECK(b.best_bid() == 102);
    CHECK(b.depth(Buy, 102) == 6);
    CHECK(!b.best_ask());
}

static void maker_remainder_keeps_its_place() {
    ReferenceBook b;
    b.limit(1, Sell, 100, 10);
    b.limit(2, Sell, 100, 10);
    b.limit(3, Buy, 100, 4);
    // Order 1 was partly filled but is still first in line.
    CHECK(b.limit(4, Buy, 100, 7) ==
          (Events{Ack{4}, Trade{4, 1, Buy, 100, 6}, Trade{4, 2, Buy, 100, 1}}));
    CHECK(b.depth(Sell, 100) == 9);
}

static void time_priority_within_level() {
    ReferenceBook b;
    b.limit(1, Sell, 100, 5);
    b.limit(2, Sell, 100, 5);
    CHECK(b.limit(3, Buy, 100, 5) == (Events{Ack{3}, Trade{3, 1, Buy, 100, 5}}));
}

static void price_priority_beats_time() {
    ReferenceBook b;
    b.limit(1, Sell, 101, 5);  // arrived first, worse price
    b.limit(2, Sell, 100, 5);
    CHECK(b.limit(3, Buy, 101, 5) == (Events{Ack{3}, Trade{3, 2, Buy, 100, 5}}));
}

static void sweep_stops_at_limit_price() {
    ReferenceBook b;
    b.limit(1, Sell, 100, 2);
    b.limit(2, Sell, 101, 2);
    b.limit(3, Sell, 103, 2);
    CHECK(b.limit(4, Buy, 102, 10) ==
          (Events{Ack{4}, Trade{4, 1, Buy, 100, 2}, Trade{4, 2, Buy, 101, 2}}));
    CHECK(b.best_ask() == 103);
    CHECK(b.depth(Buy, 102) == 6);
}

// --- market orders ---

static void market_buy_sweeps_levels() {
    ReferenceBook b;
    b.limit(1, Sell, 100, 2);
    b.limit(2, Sell, 105, 2);
    CHECK(b.market(3, Buy, 3) ==
          (Events{Ack{3}, Trade{3, 1, Buy, 100, 2}, Trade{3, 2, Buy, 105, 1}}));
    CHECK(b.depth(Sell, 105) == 1);
}

static void market_remainder_is_cancelled_not_rested() {
    ReferenceBook b;
    b.limit(1, Buy, 100, 3);
    CHECK(b.market(2, Sell, 5) ==
          (Events{Ack{2}, Trade{2, 1, Sell, 100, 3}, Cancelled{2, 2}}));
    CHECK(b.order_count() == 0);
    CHECK(!b.best_ask());
}

static void market_on_empty_book_cancels_everything() {
    ReferenceBook b;
    CHECK(b.market(1, Buy, 5) == (Events{Ack{1}, Cancelled{1, 5}}));
}

// --- cancels ---

static void cancel_resting_order() {
    ReferenceBook b;
    b.limit(1, Buy, 100, 10);
    CHECK(b.cancel(1) == Events{Cancelled{1, 10}});
    CHECK(!b.best_bid());
    CHECK(b.order_count() == 0);
}

static void cancel_partly_filled_order_returns_remaining() {
    ReferenceBook b;
    b.limit(1, Sell, 100, 10);
    b.limit(2, Buy, 100, 3);
    CHECK(b.cancel(1) == Events{Cancelled{1, 7}});
}

static void cancel_middle_order_keeps_others_in_line() {
    ReferenceBook b;
    b.limit(1, Sell, 100, 1);
    b.limit(2, Sell, 100, 1);
    b.limit(3, Sell, 100, 1);
    b.cancel(2);
    CHECK(b.limit(4, Buy, 100, 2) ==
          (Events{Ack{4}, Trade{4, 1, Buy, 100, 1}, Trade{4, 3, Buy, 100, 1}}));
}

static void cancel_unknown_id_rejected() {
    ReferenceBook b;
    CHECK(b.cancel(42) == Events{Rejected{42, RejectReason::UnknownId}});
}

static void cancel_filled_order_rejected() {
    ReferenceBook b;
    b.limit(1, Sell, 100, 5);
    b.limit(2, Buy, 100, 5);
    CHECK(b.cancel(1) == Events{Rejected{1, RejectReason::UnknownId}});
    CHECK(b.cancel(2) == Events{Rejected{2, RejectReason::UnknownId}});
}

// --- rejects ---

static void duplicate_live_id_rejected() {
    ReferenceBook b;
    b.limit(1, Buy, 100, 5);
    CHECK(b.limit(1, Sell, 90, 5) == Events{Rejected{1, RejectReason::DuplicateId}});
    CHECK(b.market(1, Sell, 5) == Events{Rejected{1, RejectReason::DuplicateId}});
    CHECK(b.depth(Buy, 100) == 5);
    CHECK(!b.best_ask());
}

static void id_reusable_after_order_is_gone() {
    ReferenceBook b;
    b.limit(1, Buy, 100, 5);
    b.cancel(1);
    CHECK(b.limit(1, Buy, 101, 5) == Events{Ack{1}});
}

static void zero_qty_rejected() {
    ReferenceBook b;
    CHECK(b.limit(1, Buy, 100, 0) == Events{Rejected{1, RejectReason::ZeroQty}});
    CHECK(b.market(2, Buy, 0) == Events{Rejected{2, RejectReason::ZeroQty}});
    CHECK(b.order_count() == 0);
}

static void non_positive_price_rejected() {
    ReferenceBook b;
    CHECK(b.limit(1, Buy, 0, 5) == Events{Rejected{1, RejectReason::NonPositivePrice}});
    CHECK(b.limit(2, Sell, -3, 5) == Events{Rejected{2, RejectReason::NonPositivePrice}});
    CHECK(b.order_count() == 0);
}

static void large_qty_does_not_overflow() {
    ReferenceBook b;
    const Qty btc = 100'000'000;  // 1 BTC in 1e-8 lots
    b.limit(1, Sell, 100, 50 * btc);
    b.limit(2, Sell, 100, 50 * btc);
    CHECK(b.depth(Sell, 100) == 100 * btc);
}

int main() {
    struct Case {
        const char* name;
        void (*fn)();
    };
    const Case cases[] = {
        {"limit_buy_on_empty_book_rests", limit_buy_on_empty_book_rests},
        {"limit_sell_on_empty_book_rests", limit_sell_on_empty_book_rests},
        {"non_crossing_orders_both_rest", non_crossing_orders_both_rest},
        {"orders_at_same_price_add_to_level", orders_at_same_price_add_to_level},
        {"crossing_buy_trades_at_maker_price", crossing_buy_trades_at_maker_price},
        {"crossing_sell_hits_highest_bid_first", crossing_sell_hits_highest_bid_first},
        {"taker_remainder_rests_at_its_limit", taker_remainder_rests_at_its_limit},
        {"maker_remainder_keeps_its_place", maker_remainder_keeps_its_place},
        {"time_priority_within_level", time_priority_within_level},
        {"price_priority_beats_time", price_priority_beats_time},
        {"sweep_stops_at_limit_price", sweep_stops_at_limit_price},
        {"market_buy_sweeps_levels", market_buy_sweeps_levels},
        {"market_remainder_is_cancelled_not_rested", market_remainder_is_cancelled_not_rested},
        {"market_on_empty_book_cancels_everything", market_on_empty_book_cancels_everything},
        {"cancel_resting_order", cancel_resting_order},
        {"cancel_partly_filled_order_returns_remaining", cancel_partly_filled_order_returns_remaining},
        {"cancel_middle_order_keeps_others_in_line", cancel_middle_order_keeps_others_in_line},
        {"cancel_unknown_id_rejected", cancel_unknown_id_rejected},
        {"cancel_filled_order_rejected", cancel_filled_order_rejected},
        {"duplicate_live_id_rejected", duplicate_live_id_rejected},
        {"id_reusable_after_order_is_gone", id_reusable_after_order_is_gone},
        {"zero_qty_rejected", zero_qty_rejected},
        {"non_positive_price_rejected", non_positive_price_rejected},
        {"large_qty_does_not_overflow", large_qty_does_not_overflow},
    };
    for (const Case& c : cases) {
        int before = failures;
        c.fn();
        std::printf("%s %s\n", failures == before ? "PASS" : "FAIL", c.name);
    }
    std::printf("%zu cases, %d failed checks\n", std::size(cases), failures);
    return failures == 0 ? 0 : 1;
}
