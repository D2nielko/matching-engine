#include "me/engine.hpp"
#include "me/reference_book.hpp"
#include "test_util.hpp"

using namespace me;

constexpr Side Buy  = Side::Buy;
constexpr Side Sell = Side::Sell;

// --- resting ---

template <class Book>
void limit_buy_on_empty_book_rests() {
    Book b;
    CHECK(b.limit(1, Buy, 100, 10) == Events{Ack{1}});
    CHECK(b.best_bid() == 100);
    CHECK(!b.best_ask());
    CHECK(b.depth(Buy, 100) == 10);
}

template <class Book>
void limit_sell_on_empty_book_rests() {
    Book b;
    CHECK(b.limit(1, Sell, 100, 10) == Events{Ack{1}});
    CHECK(b.best_ask() == 100);
    CHECK(!b.best_bid());
}

template <class Book>
void non_crossing_orders_both_rest() {
    Book b;
    b.limit(1, Buy, 99, 5);
    CHECK(b.limit(2, Sell, 101, 5) == Events{Ack{2}});
    CHECK(b.best_bid() == 99);
    CHECK(b.best_ask() == 101);
    CHECK(b.order_count() == 2);
}

template <class Book>
void orders_at_same_price_add_to_level() {
    Book b;
    b.limit(1, Buy, 100, 3);
    b.limit(2, Buy, 100, 4);
    CHECK(b.depth(Buy, 100) == 7);
}

// --- matching ---

template <class Book>
void crossing_buy_trades_at_maker_price() {
    Book b;
    b.limit(1, Sell, 100, 10);
    CHECK(b.limit(2, Buy, 105, 10) == (Events{Ack{2}, Trade{2, 1, Buy, 100, 10}}));
    CHECK(b.order_count() == 0);
    CHECK(!b.best_bid() && !b.best_ask());
}

template <class Book>
void crossing_sell_hits_highest_bid_first() {
    Book b;
    b.limit(1, Buy, 99, 5);
    b.limit(2, Buy, 101, 5);
    CHECK(b.limit(3, Sell, 99, 5) == (Events{Ack{3}, Trade{3, 2, Sell, 101, 5}}));
    CHECK(b.best_bid() == 99);
}

template <class Book>
void taker_remainder_rests_at_its_limit() {
    Book b;
    b.limit(1, Sell, 100, 4);
    CHECK(b.limit(2, Buy, 102, 10) == (Events{Ack{2}, Trade{2, 1, Buy, 100, 4}}));
    CHECK(b.best_bid() == 102);
    CHECK(b.depth(Buy, 102) == 6);
    CHECK(!b.best_ask());
}

template <class Book>
void maker_remainder_keeps_its_place() {
    Book b;
    b.limit(1, Sell, 100, 10);
    b.limit(2, Sell, 100, 10);
    b.limit(3, Buy, 100, 4);
    // Order 1 was partly filled but is still first in line.
    CHECK(b.limit(4, Buy, 100, 7) ==
          (Events{Ack{4}, Trade{4, 1, Buy, 100, 6}, Trade{4, 2, Buy, 100, 1}}));
    CHECK(b.depth(Sell, 100) == 9);
}

template <class Book>
void time_priority_within_level() {
    Book b;
    b.limit(1, Sell, 100, 5);
    b.limit(2, Sell, 100, 5);
    CHECK(b.limit(3, Buy, 100, 5) == (Events{Ack{3}, Trade{3, 1, Buy, 100, 5}}));
}

template <class Book>
void price_priority_beats_time() {
    Book b;
    b.limit(1, Sell, 101, 5);  // arrived first, worse price
    b.limit(2, Sell, 100, 5);
    CHECK(b.limit(3, Buy, 101, 5) == (Events{Ack{3}, Trade{3, 2, Buy, 100, 5}}));
}

template <class Book>
void sweep_stops_at_limit_price() {
    Book b;
    b.limit(1, Sell, 100, 2);
    b.limit(2, Sell, 101, 2);
    b.limit(3, Sell, 103, 2);
    CHECK(b.limit(4, Buy, 102, 10) ==
          (Events{Ack{4}, Trade{4, 1, Buy, 100, 2}, Trade{4, 2, Buy, 101, 2}}));
    CHECK(b.best_ask() == 103);
    CHECK(b.depth(Buy, 102) == 6);
}

// --- market orders ---

template <class Book>
void market_buy_sweeps_levels() {
    Book b;
    b.limit(1, Sell, 100, 2);
    b.limit(2, Sell, 105, 2);
    CHECK(b.market(3, Buy, 3) ==
          (Events{Ack{3}, Trade{3, 1, Buy, 100, 2}, Trade{3, 2, Buy, 105, 1}}));
    CHECK(b.depth(Sell, 105) == 1);
}

template <class Book>
void market_remainder_is_cancelled_not_rested() {
    Book b;
    b.limit(1, Buy, 100, 3);
    CHECK(b.market(2, Sell, 5) ==
          (Events{Ack{2}, Trade{2, 1, Sell, 100, 3}, Cancelled{2, 2}}));
    CHECK(b.order_count() == 0);
    CHECK(!b.best_ask());
}

template <class Book>
void market_on_empty_book_cancels_everything() {
    Book b;
    CHECK(b.market(1, Buy, 5) == (Events{Ack{1}, Cancelled{1, 5}}));
}

// --- cancels ---

template <class Book>
void cancel_resting_order() {
    Book b;
    b.limit(1, Buy, 100, 10);
    CHECK(b.cancel(1) == Events{Cancelled{1, 10}});
    CHECK(!b.best_bid());
    CHECK(b.order_count() == 0);
}

template <class Book>
void cancel_partly_filled_order_returns_remaining() {
    Book b;
    b.limit(1, Sell, 100, 10);
    b.limit(2, Buy, 100, 3);
    CHECK(b.cancel(1) == Events{Cancelled{1, 7}});
}

template <class Book>
void cancel_middle_order_keeps_others_in_line() {
    Book b;
    b.limit(1, Sell, 100, 1);
    b.limit(2, Sell, 100, 1);
    b.limit(3, Sell, 100, 1);
    b.cancel(2);
    CHECK(b.limit(4, Buy, 100, 2) ==
          (Events{Ack{4}, Trade{4, 1, Buy, 100, 1}, Trade{4, 3, Buy, 100, 1}}));
}

template <class Book>
void cancel_unknown_id_rejected() {
    Book b;
    CHECK(b.cancel(42) == Events{Rejected{42, RejectReason::UnknownId}});
}

template <class Book>
void cancel_filled_order_rejected() {
    Book b;
    b.limit(1, Sell, 100, 5);
    b.limit(2, Buy, 100, 5);
    CHECK(b.cancel(1) == Events{Rejected{1, RejectReason::UnknownId}});
    CHECK(b.cancel(2) == Events{Rejected{2, RejectReason::UnknownId}});
}

// --- rejects ---

template <class Book>
void duplicate_live_id_rejected() {
    Book b;
    b.limit(1, Buy, 100, 5);
    CHECK(b.limit(1, Sell, 90, 5) == Events{Rejected{1, RejectReason::DuplicateId}});
    CHECK(b.market(1, Sell, 5) == Events{Rejected{1, RejectReason::DuplicateId}});
    CHECK(b.depth(Buy, 100) == 5);
    CHECK(!b.best_ask());
}

template <class Book>
void id_reusable_after_order_is_gone() {
    Book b;
    b.limit(1, Buy, 100, 5);
    b.cancel(1);
    CHECK(b.limit(1, Buy, 101, 5) == Events{Ack{1}});
}

template <class Book>
void zero_qty_rejected() {
    Book b;
    CHECK(b.limit(1, Buy, 100, 0) == Events{Rejected{1, RejectReason::ZeroQty}});
    CHECK(b.market(2, Buy, 0) == Events{Rejected{2, RejectReason::ZeroQty}});
    CHECK(b.order_count() == 0);
}

template <class Book>
void non_positive_price_rejected() {
    Book b;
    CHECK(b.limit(1, Buy, 0, 5) == Events{Rejected{1, RejectReason::NonPositivePrice}});
    CHECK(b.limit(2, Sell, -3, 5) == Events{Rejected{2, RejectReason::NonPositivePrice}});
    CHECK(b.order_count() == 0);
}

template <class Book>
void large_qty_does_not_overflow() {
    Book b;
    const Qty btc = 100'000'000;  // 1 BTC in 1e-8 lots
    b.limit(1, Sell, 100, 50 * btc);
    b.limit(2, Sell, 100, 50 * btc);
    CHECK(b.depth(Sell, 100) == 100 * btc);
}

// --- IOC ---

template <class Book>
void ioc_trades_what_crosses_and_cancels_rest() {
    Book b;
    b.limit(1, Sell, 100, 4);
    CHECK(b.ioc(2, Buy, 101, 10) == (Events{Ack{2}, Trade{2, 1, Buy, 100, 4}, Cancelled{2, 6}}));
    CHECK(!b.best_bid() && !b.best_ask());
}

template <class Book>
void ioc_with_nothing_crossing_cancels_all() {
    Book b;
    b.limit(1, Sell, 105, 5);
    CHECK(b.ioc(2, Buy, 100, 3) == (Events{Ack{2}, Cancelled{2, 3}}));
    CHECK(b.best_ask() == 105);
    CHECK(!b.best_bid());
}

template <class Book>
void ioc_respects_limit_price() {
    Book b;
    b.limit(1, Sell, 100, 2);
    b.limit(2, Sell, 102, 2);
    CHECK(b.ioc(3, Buy, 101, 5) == (Events{Ack{3}, Trade{3, 1, Buy, 100, 2}, Cancelled{3, 3}}));
    CHECK(b.depth(Sell, 102) == 2);
}

// --- FOK ---

template <class Book>
void fok_fills_completely_across_levels() {
    Book b;
    b.limit(1, Sell, 100, 2);
    b.limit(2, Sell, 101, 3);
    CHECK(b.fok(3, Buy, 101, 5) ==
          (Events{Ack{3}, Trade{3, 1, Buy, 100, 2}, Trade{3, 2, Buy, 101, 3}}));
    CHECK(b.order_count() == 0);
}

template <class Book>
void fok_without_enough_liquidity_does_nothing() {
    Book b;
    b.limit(1, Sell, 100, 2);
    b.limit(2, Sell, 101, 3);
    CHECK(b.fok(3, Buy, 101, 6) == (Events{Ack{3}, Cancelled{3, 6}}));
    CHECK(b.depth(Sell, 100) == 2);
    CHECK(b.depth(Sell, 101) == 3);
}

template <class Book>
void fok_ignores_liquidity_beyond_its_limit() {
    Book b;
    b.limit(1, Sell, 100, 2);
    b.limit(2, Sell, 102, 10);
    CHECK(b.fok(3, Buy, 101, 3) == (Events{Ack{3}, Cancelled{3, 3}}));
}

// --- post-only ---

template <class Book>
void post_only_rests_when_not_crossing() {
    Book b;
    b.limit(1, Sell, 101, 5);
    CHECK(b.post_only(2, Buy, 100, 5) == Events{Ack{2}});
    CHECK(b.best_bid() == 100);
}

template <class Book>
void post_only_that_would_trade_is_rejected() {
    Book b;
    b.limit(1, Sell, 100, 5);
    CHECK(b.post_only(2, Buy, 100, 5) == Events{Rejected{2, RejectReason::WouldTrade}});
    CHECK(!b.best_bid());
    CHECK(b.depth(Sell, 100) == 5);
}

template <class Book>
void post_only_on_empty_book_rests() {
    Book b;
    CHECK(b.post_only(1, Sell, 100, 5) == Events{Ack{1}});
    CHECK(b.best_ask() == 100);
}

// --- modify ---

template <class Book>
void modify_smaller_size_keeps_place() {
    Book b;
    b.limit(1, Sell, 100, 5);
    b.limit(2, Sell, 100, 5);
    CHECK(b.modify(1, 100, 3) == Events{Modified{1, 100, 3}});
    CHECK(b.limit(3, Buy, 100, 4) ==
          (Events{Ack{3}, Trade{3, 1, Buy, 100, 3}, Trade{3, 2, Buy, 100, 1}}));
}

template <class Book>
void modify_same_values_keeps_place() {
    Book b;
    b.limit(1, Sell, 100, 5);
    b.limit(2, Sell, 100, 5);
    CHECK(b.modify(1, 100, 5) == Events{Modified{1, 100, 5}});
    CHECK(b.limit(3, Buy, 100, 1) == (Events{Ack{3}, Trade{3, 1, Buy, 100, 1}}));
}

template <class Book>
void modify_bigger_size_loses_place() {
    Book b;
    b.limit(1, Sell, 100, 5);
    b.limit(2, Sell, 100, 5);
    CHECK(b.modify(1, 100, 8) == Events{Modified{1, 100, 8}});
    CHECK(b.depth(Sell, 100) == 13);
    CHECK(b.limit(3, Buy, 100, 5) == (Events{Ack{3}, Trade{3, 2, Buy, 100, 5}}));
}

template <class Book>
void modify_new_price_loses_place() {
    Book b;
    b.limit(1, Sell, 101, 5);
    b.limit(2, Sell, 100, 5);
    CHECK(b.modify(1, 100, 5) == Events{Modified{1, 100, 5}});
    CHECK(!b.depth(Sell, 101));
    CHECK(b.limit(3, Buy, 100, 5) == (Events{Ack{3}, Trade{3, 2, Buy, 100, 5}}));
}

template <class Book>
void modify_into_cross_trades() {
    Book b;
    b.limit(1, Buy, 99, 10);
    b.limit(2, Sell, 101, 4);
    CHECK(b.modify(1, 101, 10) == (Events{Modified{1, 101, 10}, Trade{1, 2, Buy, 101, 4}}));
    CHECK(b.best_bid() == 101);
    CHECK(b.depth(Buy, 101) == 6);
    CHECK(!b.best_ask());
}

template <class Book>
void modify_post_only_into_cross_rejected() {
    Book b;
    b.post_only(1, Buy, 99, 5);
    b.limit(2, Sell, 101, 5);
    CHECK(b.modify(1, 101, 5) == Events{Rejected{1, RejectReason::WouldTrade}});
    CHECK(b.depth(Buy, 99) == 5);
}

template <class Book>
void modify_rejects_leave_order_unchanged() {
    Book b;
    b.limit(1, Buy, 100, 5);
    CHECK(b.modify(9, 100, 5) == Events{Rejected{9, RejectReason::UnknownId}});
    CHECK(b.modify(1, 100, 0) == Events{Rejected{1, RejectReason::ZeroQty}});
    CHECK(b.modify(1, 0, 5) == Events{Rejected{1, RejectReason::NonPositivePrice}});
    CHECK(b.depth(Buy, 100) == 5);
}

#define BOOK_CASES(X)                              \
    X(limit_buy_on_empty_book_rests)               \
    X(limit_sell_on_empty_book_rests)              \
    X(non_crossing_orders_both_rest)               \
    X(orders_at_same_price_add_to_level)           \
    X(crossing_buy_trades_at_maker_price)          \
    X(crossing_sell_hits_highest_bid_first)        \
    X(taker_remainder_rests_at_its_limit)          \
    X(maker_remainder_keeps_its_place)             \
    X(time_priority_within_level)                  \
    X(price_priority_beats_time)                   \
    X(sweep_stops_at_limit_price)                  \
    X(market_buy_sweeps_levels)                    \
    X(market_remainder_is_cancelled_not_rested)    \
    X(market_on_empty_book_cancels_everything)     \
    X(cancel_resting_order)                        \
    X(cancel_partly_filled_order_returns_remaining) \
    X(cancel_middle_order_keeps_others_in_line)    \
    X(cancel_unknown_id_rejected)                  \
    X(cancel_filled_order_rejected)                \
    X(duplicate_live_id_rejected)                  \
    X(id_reusable_after_order_is_gone)             \
    X(zero_qty_rejected)                           \
    X(non_positive_price_rejected)                 \
    X(large_qty_does_not_overflow)                 \
    X(ioc_trades_what_crosses_and_cancels_rest)    \
    X(ioc_with_nothing_crossing_cancels_all)       \
    X(ioc_respects_limit_price)                    \
    X(fok_fills_completely_across_levels)          \
    X(fok_without_enough_liquidity_does_nothing)   \
    X(fok_ignores_liquidity_beyond_its_limit)      \
    X(post_only_rests_when_not_crossing)           \
    X(post_only_that_would_trade_is_rejected)      \
    X(post_only_on_empty_book_rests)               \
    X(modify_smaller_size_keeps_place)             \
    X(modify_same_values_keeps_place)              \
    X(modify_bigger_size_loses_place)              \
    X(modify_new_price_loses_place)                \
    X(modify_into_cross_trades)                    \
    X(modify_post_only_into_cross_rejected)        \
    X(modify_rejects_leave_order_unchanged)

// Every case runs against both implementations.
template <class Book>
int run_all(const char* book_name) {
    int cases = 0;
#define RUN(name)                                                                       \
    {                                                                                   \
        int before = failures;                                                          \
        name<Book>();                                                                   \
        std::printf("%s %s/%s\n", failures == before ? "PASS" : "FAIL", book_name, #name); \
        ++cases;                                                                        \
    }
    BOOK_CASES(RUN)
#undef RUN
    return cases;
}

int main() {
    int cases = run_all<ReferenceBook>("reference") + run_all<Engine>("engine");
    std::printf("%d cases, %d failed checks\n", cases, failures);
    return failures == 0 ? 0 : 1;
}
