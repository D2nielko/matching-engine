#pragma once

#include <algorithm>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>

#include "me/events.hpp"
#include "me/types.hpp"

namespace me {

// Deliberately simple order book. It is the oracle every faster implementation is tested against,
// so clarity wins over speed here. Rules are in docs/rules.md.
class ReferenceBook {
public:
    Events limit(OrderId id, Side side, Price price, Qty qty);
    Events market(OrderId id, Side side, Qty qty);
    Events cancel(OrderId id);

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Qty depth(Side side, Price price) const;  // total resting qty at one price level
    std::size_t order_count() const { return index_.size(); }

private:
    struct Order {
        OrderId id;
        Qty     qty;
    };
    using Level = std::list<Order>;  // FIFO: front has time priority

    struct Locator {
        Side             side;
        Price            price;
        Level::iterator  it;
    };

    std::map<Price, Level, std::greater<Price>> bids_;  // best (highest) first
    std::map<Price, Level>                      asks_;  // best (lowest) first
    std::unordered_map<OrderId, Locator>        index_;

    std::optional<RejectReason> check_new(OrderId id, Qty qty) const;

    template <class Book, class Crosses>
    Qty match(Book& book, OrderId taker, Side taker_side, Qty qty, Crosses crosses, Events& out);

    template <class Book>
    void rest(Book& book, Side side, OrderId id, Price price, Qty qty);

    template <class Book>
    Qty remove(Book& book, const Locator& loc);

    template <class Book>
    static Qty level_qty(const Book& book, Price price);
};

inline std::optional<RejectReason> ReferenceBook::check_new(OrderId id, Qty qty) const {
    if (index_.contains(id)) return RejectReason::DuplicateId;
    if (qty == 0) return RejectReason::ZeroQty;
    return std::nullopt;
}

inline Events ReferenceBook::limit(OrderId id, Side side, Price price, Qty qty) {
    Events out;
    if (auto r = check_new(id, qty)) {
        out.push_back(Rejected{id, *r});
        return out;
    }
    if (price <= 0) {
        out.push_back(Rejected{id, RejectReason::NonPositivePrice});
        return out;
    }
    out.push_back(Ack{id});
    if (side == Side::Buy) {
        qty = match(asks_, id, side, qty, [price](Price p) { return p <= price; }, out);
        if (qty > 0) rest(bids_, side, id, price, qty);
    } else {
        qty = match(bids_, id, side, qty, [price](Price p) { return p >= price; }, out);
        if (qty > 0) rest(asks_, side, id, price, qty);
    }
    return out;
}

inline Events ReferenceBook::market(OrderId id, Side side, Qty qty) {
    Events out;
    if (auto r = check_new(id, qty)) {
        out.push_back(Rejected{id, *r});
        return out;
    }
    out.push_back(Ack{id});
    auto any_price = [](Price) { return true; };
    qty = side == Side::Buy ? match(asks_, id, side, qty, any_price, out)
                            : match(bids_, id, side, qty, any_price, out);
    if (qty > 0) out.push_back(Cancelled{id, qty});
    return out;
}

inline Events ReferenceBook::cancel(OrderId id) {
    Events out;
    auto found = index_.find(id);
    if (found == index_.end()) {
        out.push_back(Rejected{id, RejectReason::UnknownId});
        return out;
    }
    Locator loc = found->second;
    index_.erase(found);
    Qty qty = loc.side == Side::Buy ? remove(bids_, loc) : remove(asks_, loc);
    out.push_back(Cancelled{id, qty});
    return out;
}

inline std::optional<Price> ReferenceBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

inline std::optional<Price> ReferenceBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

inline Qty ReferenceBook::depth(Side side, Price price) const {
    return side == Side::Buy ? level_qty(bids_, price) : level_qty(asks_, price);
}

template <class Book, class Crosses>
Qty ReferenceBook::match(Book& book, OrderId taker, Side taker_side, Qty qty, Crosses crosses,
                         Events& out) {
    while (qty > 0 && !book.empty() && crosses(book.begin()->first)) {
        auto   level  = book.begin();
        Level& orders = level->second;
        while (qty > 0 && !orders.empty()) {
            Order& maker = orders.front();
            Qty    fill  = std::min(qty, maker.qty);
            out.push_back(Trade{taker, maker.id, taker_side, level->first, fill});
            qty -= fill;
            maker.qty -= fill;
            if (maker.qty == 0) {
                index_.erase(maker.id);
                orders.pop_front();
            }
        }
        if (orders.empty()) book.erase(level);
    }
    return qty;
}

template <class Book>
void ReferenceBook::rest(Book& book, Side side, OrderId id, Price price, Qty qty) {
    Level& orders = book[price];
    orders.push_back(Order{id, qty});
    index_.emplace(id, Locator{side, price, std::prev(orders.end())});
}

template <class Book>
Qty ReferenceBook::remove(Book& book, const Locator& loc) {
    auto   level  = book.find(loc.price);
    Qty    qty    = loc.it->qty;
    level->second.erase(loc.it);
    if (level->second.empty()) book.erase(level);
    return qty;
}

template <class Book>
Qty ReferenceBook::level_qty(const Book& book, Price price) {
    auto level = book.find(price);
    if (level == book.end()) return 0;
    Qty total = 0;
    for (const Order& o : level->second) total += o.qty;
    return total;
}

}  // namespace me
