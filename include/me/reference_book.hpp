#pragma once

#include <algorithm>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>

#include "me/commands.hpp"
#include "me/events.hpp"
#include "me/types.hpp"

namespace me {

// Deliberately simple order book. It is the oracle every faster implementation is tested against,
// so clarity wins over speed here. Rules are in docs/rules.md.
class ReferenceBook {
public:
    Events apply(const Command& cmd);
    Events submit(const NewOrder& o);
    Events cancel(OrderId id);
    Events modify(OrderId id, Price price, Qty qty);

    Events limit(OrderId id, Side side, Price price, Qty qty) { return submit({id, side, OrderType::Limit, price, qty}); }
    Events market(OrderId id, Side side, Qty qty) { return submit({id, side, OrderType::Market, 0, qty}); }
    Events ioc(OrderId id, Side side, Price price, Qty qty) { return submit({id, side, OrderType::IOC, price, qty}); }
    Events fok(OrderId id, Side side, Price price, Qty qty) { return submit({id, side, OrderType::FOK, price, qty}); }
    Events post_only(OrderId id, Side side, Price price, Qty qty) { return submit({id, side, OrderType::PostOnly, price, qty}); }

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Qty depth(Side side, Price price) const;  // total resting qty at one price level
    std::size_t order_count() const { return index_.size(); }

private:
    struct Order {
        OrderId id;
        Qty     qty;
        bool    post_only;
    };
    using Level = std::list<Order>;  // FIFO: front has time priority

    struct Locator {
        Side            side;
        Price           price;
        Level::iterator it;
    };

    std::map<Price, Level, std::greater<Price>> bids_;  // best (highest) first
    std::map<Price, Level>                      asks_;  // best (lowest) first
    std::unordered_map<OrderId, Locator>        index_;

    // Call f with the map for `side`. The two maps have different types, so f is a generic lambda.
    template <class F> decltype(auto) on_side(Side side, F f) { return side == Side::Buy ? f(bids_) : f(asks_); }
    template <class F> decltype(auto) on_side(Side side, F f) const { return side == Side::Buy ? f(bids_) : f(asks_); }
    static Side opposite(Side s) { return s == Side::Buy ? Side::Sell : Side::Buy; }
    // Can a `taker_side` order with limit `limit` trade against a resting order at `resting`?
    static bool crosses(Side taker_side, Price resting, Price limit) {
        return taker_side == Side::Buy ? resting <= limit : resting >= limit;
    }

    bool would_trade(Side side, Price price) const;
    Qty available(Side side, Price price, Qty wanted) const;
    Qty take(OrderId taker, Side side, std::optional<Price> limit, Qty qty, Events& out);
    void rest(Side side, OrderId id, Price price, Qty qty, bool post_only);
    void erase(const Locator& loc);
};

inline Events ReferenceBook::apply(const Command& cmd) {
    if (auto* o = std::get_if<NewOrder>(&cmd)) return submit(*o);
    if (auto* c = std::get_if<Cancel>(&cmd)) return cancel(c->id);
    const auto& m = std::get<Modify>(cmd);
    return modify(m.id, m.price, m.qty);
}

inline Events ReferenceBook::submit(const NewOrder& o) {
    Events out;
    auto reject = [&](RejectReason r) { out.push_back(Rejected{o.id, r}); return out; };
    if (index_.contains(o.id)) return reject(RejectReason::DuplicateId);
    if (o.qty == 0) return reject(RejectReason::ZeroQty);
    if (o.type != OrderType::Market && o.price <= 0) return reject(RejectReason::NonPositivePrice);
    if (o.type == OrderType::PostOnly && would_trade(o.side, o.price)) return reject(RejectReason::WouldTrade);

    out.push_back(Ack{o.id});
    if (o.type == OrderType::FOK && available(o.side, o.price, o.qty) < o.qty) {
        out.push_back(Cancelled{o.id, o.qty});
        return out;
    }
    std::optional<Price> limit;
    if (o.type != OrderType::Market) limit = o.price;
    Qty left = take(o.id, o.side, limit, o.qty, out);
    if (left == 0) return out;
    if (o.type == OrderType::Limit || o.type == OrderType::PostOnly) {
        rest(o.side, o.id, o.price, left, o.type == OrderType::PostOnly);
    } else {
        out.push_back(Cancelled{o.id, left});
    }
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
    out.push_back(Cancelled{id, loc.it->qty});
    erase(loc);
    return out;
}

inline Events ReferenceBook::modify(OrderId id, Price price, Qty qty) {
    Events out;
    auto reject = [&](RejectReason r) { out.push_back(Rejected{id, r}); return out; };
    auto found = index_.find(id);
    if (found == index_.end()) return reject(RejectReason::UnknownId);
    if (qty == 0) return reject(RejectReason::ZeroQty);
    if (price <= 0) return reject(RejectReason::NonPositivePrice);

    Locator loc = found->second;
    Order&  order = *loc.it;
    // Same price and no bigger: shrink in place, keeping time priority.
    if (price == loc.price && qty <= order.qty) {
        order.qty = qty;
        out.push_back(Modified{id, price, qty});
        return out;
    }
    if (order.post_only && would_trade(loc.side, price)) return reject(RejectReason::WouldTrade);

    // Otherwise the order loses its place: take it out and send it back in as a limit order.
    bool post_only = order.post_only;
    erase(loc);
    out.push_back(Modified{id, price, qty});
    Qty left = take(id, loc.side, price, qty, out);
    if (left > 0) rest(loc.side, id, price, left, post_only);
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
    return on_side(side, [&](const auto& book) -> Qty {
        auto level = book.find(price);
        if (level == book.end()) return 0;
        Qty total = 0;
        for (const Order& o : level->second) total += o.qty;
        return total;
    });
}

inline bool ReferenceBook::would_trade(Side side, Price price) const {
    return on_side(opposite(side), [&](const auto& book) {
        return !book.empty() && crosses(side, book.begin()->first, price);
    });
}

// Opposite-side quantity reachable at `price` or better, counting no further than `wanted`.
inline Qty ReferenceBook::available(Side side, Price price, Qty wanted) const {
    return on_side(opposite(side), [&](const auto& book) {
        Qty total = 0;
        for (auto level = book.begin(); level != book.end() && total < wanted; ++level) {
            if (!crosses(side, level->first, price)) break;
            for (const Order& o : level->second) total += o.qty;
        }
        return total;
    });
}

// Match an incoming order against the opposite side. Returns the quantity left over.
inline Qty ReferenceBook::take(OrderId taker, Side side, std::optional<Price> limit, Qty qty, Events& out) {
    return on_side(opposite(side), [&](auto& book) {
        while (qty > 0 && !book.empty()) {
            auto level = book.begin();
            if (limit && !crosses(side, level->first, *limit)) break;
            Level& orders = level->second;
            while (qty > 0 && !orders.empty()) {
                Order& maker = orders.front();
                Qty    fill  = std::min(qty, maker.qty);
                out.push_back(Trade{taker, maker.id, side, level->first, fill});
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
    });
}

inline void ReferenceBook::rest(Side side, OrderId id, Price price, Qty qty, bool post_only) {
    on_side(side, [&](auto& book) {
        Level& orders = book[price];
        orders.push_back(Order{id, qty, post_only});
        index_.emplace(id, Locator{side, price, std::prev(orders.end())});
    });
}

inline void ReferenceBook::erase(const Locator& loc) {
    index_.erase(loc.it->id);
    on_side(loc.side, [&](auto& book) {
        auto level = book.find(loc.price);
        level->second.erase(loc.it);
        if (level->second.empty()) book.erase(level);
    });
}

}  // namespace me
