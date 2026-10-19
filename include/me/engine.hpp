#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "me/commands.hpp"
#include "me/events.hpp"
#include "me/types.hpp"

namespace me {

// The matching engine as a state machine: step(command) appends events and touches nothing outside
// the book. It follows docs/rules.md and must produce exactly the same events as ReferenceBook.
//
// Storage differs from ReferenceBook on purpose: orders live in one pooled array and each price level
// is an intrusive doubly linked list of pool indices with a cached total. Freed slots are reused, so
// once the pool has grown, resting an order does not allocate.
class Engine {
public:
    void step(const Command& cmd, Events& out);  // appends to out
    Events step(const Command& cmd) {
        Events out;
        step(cmd, out);
        return out;
    }

    Events limit(OrderId id, Side side, Price price, Qty qty) { return step(NewOrder{id, side, OrderType::Limit, price, qty}); }
    Events market(OrderId id, Side side, Qty qty) { return step(NewOrder{id, side, OrderType::Market, 0, qty}); }
    Events ioc(OrderId id, Side side, Price price, Qty qty) { return step(NewOrder{id, side, OrderType::IOC, price, qty}); }
    Events fok(OrderId id, Side side, Price price, Qty qty) { return step(NewOrder{id, side, OrderType::FOK, price, qty}); }
    Events post_only(OrderId id, Side side, Price price, Qty qty) { return step(NewOrder{id, side, OrderType::PostOnly, price, qty}); }
    Events cancel(OrderId id) { return step(Cancel{id}); }
    Events modify(OrderId id, Price price, Qty qty) { return step(Modify{id, price, qty}); }

    std::optional<Price> best_bid() const;
    std::optional<Price> best_ask() const;
    Qty depth(Side side, Price price) const;
    std::size_t order_count() const { return index_.size(); }

    // Walks the whole book and aborts with a message if any invariant is broken. O(book size).
    void check_invariants() const;

    // FNV-1a hash of the full book: every resting order's side, price, queue position, id, qty and
    // post-only flag. Two engines with equal hashes hold the same book.
    std::uint64_t state_hash() const;

private:
    static constexpr std::uint32_t kNil = UINT32_MAX;

    struct Node {
        OrderId       id;
        Price         price;
        Qty           qty;
        std::uint32_t prev;
        std::uint32_t next;
        Side          side;
        bool          post_only;
    };
    struct Level {
        std::uint32_t head  = kNil;  // time priority: head trades first
        std::uint32_t tail  = kNil;
        Qty           total = 0;
    };

    std::vector<Node>                          nodes_;
    std::vector<std::uint32_t>                 free_;
    std::map<Price, Level, std::greater<Price>> bids_;
    std::map<Price, Level>                      asks_;
    std::unordered_map<OrderId, std::uint32_t>  index_;  // id → pool index

    template <class F> decltype(auto) on_side(Side side, F f) { return side == Side::Buy ? f(bids_) : f(asks_); }
    template <class F> decltype(auto) on_side(Side side, F f) const { return side == Side::Buy ? f(bids_) : f(asks_); }
    static Side opposite(Side s) { return s == Side::Buy ? Side::Sell : Side::Buy; }
    static bool crosses(Side taker_side, Price resting, Price limit) {
        return taker_side == Side::Buy ? resting <= limit : resting >= limit;
    }

    void on_new(const NewOrder& o, Events& out);
    void on_cancel(OrderId id, Events& out);
    void on_modify(const Modify& m, Events& out);

    bool would_trade(Side side, Price price) const;
    Qty available(Side side, Price price, Qty wanted) const;
    Qty take(OrderId taker, Side side, std::optional<Price> limit, Qty qty, Events& out);
    void rest(Side side, OrderId id, Price price, Qty qty, bool post_only);
    void remove(std::uint32_t idx);

    std::uint32_t alloc();
    void release(std::uint32_t idx) { free_.push_back(idx); }
    void link_back(Level& level, std::uint32_t idx);
    void unlink(Level& level, std::uint32_t idx);
};

inline void Engine::step(const Command& cmd, Events& out) {
    if (auto* o = std::get_if<NewOrder>(&cmd)) on_new(*o, out);
    else if (auto* c = std::get_if<Cancel>(&cmd)) on_cancel(c->id, out);
    else on_modify(std::get<Modify>(cmd), out);
#ifdef ME_CHECK_INVARIANTS
    check_invariants();
#endif
}

inline void Engine::on_new(const NewOrder& o, Events& out) {
    auto reject = [&](RejectReason r) { out.push_back(Rejected{o.id, r}); };
    if (index_.contains(o.id)) return reject(RejectReason::DuplicateId);
    if (o.qty == 0) return reject(RejectReason::ZeroQty);
    if (o.type != OrderType::Market && o.price <= 0) return reject(RejectReason::NonPositivePrice);
    if (o.type == OrderType::PostOnly && would_trade(o.side, o.price)) return reject(RejectReason::WouldTrade);

    out.push_back(Ack{o.id});
    if (o.type == OrderType::FOK && available(o.side, o.price, o.qty) < o.qty) {
        out.push_back(Cancelled{o.id, o.qty});
        return;
    }
    std::optional<Price> limit;
    if (o.type != OrderType::Market) limit = o.price;
    Qty left = take(o.id, o.side, limit, o.qty, out);
    if (left == 0) return;
    if (o.type == OrderType::Limit || o.type == OrderType::PostOnly) {
        rest(o.side, o.id, o.price, left, o.type == OrderType::PostOnly);
    } else {
        out.push_back(Cancelled{o.id, left});
    }
}

inline void Engine::on_cancel(OrderId id, Events& out) {
    auto found = index_.find(id);
    if (found == index_.end()) {
        out.push_back(Rejected{id, RejectReason::UnknownId});
        return;
    }
    std::uint32_t idx = found->second;
    out.push_back(Cancelled{id, nodes_[idx].qty});
    remove(idx);
}

inline void Engine::on_modify(const Modify& m, Events& out) {
    auto reject = [&](RejectReason r) { out.push_back(Rejected{m.id, r}); };
    auto found = index_.find(m.id);
    if (found == index_.end()) return reject(RejectReason::UnknownId);
    if (m.qty == 0) return reject(RejectReason::ZeroQty);
    if (m.price <= 0) return reject(RejectReason::NonPositivePrice);

    std::uint32_t idx  = found->second;
    Node&         node = nodes_[idx];
    if (m.price == node.price && m.qty <= node.qty) {
        on_side(node.side, [&](auto& book) { book.find(node.price)->second.total -= node.qty - m.qty; });
        node.qty = m.qty;
        out.push_back(Modified{m.id, m.price, m.qty});
        return;
    }
    if (node.post_only && would_trade(node.side, m.price)) return reject(RejectReason::WouldTrade);

    Side side      = node.side;
    bool post_only = node.post_only;
    remove(idx);
    out.push_back(Modified{m.id, m.price, m.qty});
    Qty left = take(m.id, side, m.price, m.qty, out);
    if (left > 0) rest(side, m.id, m.price, left, post_only);
}

inline std::optional<Price> Engine::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

inline std::optional<Price> Engine::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

inline Qty Engine::depth(Side side, Price price) const {
    return on_side(side, [&](const auto& book) -> Qty {
        auto level = book.find(price);
        return level == book.end() ? 0 : level->second.total;
    });
}

inline bool Engine::would_trade(Side side, Price price) const {
    return on_side(opposite(side), [&](const auto& book) {
        return !book.empty() && crosses(side, book.begin()->first, price);
    });
}

inline Qty Engine::available(Side side, Price price, Qty wanted) const {
    return on_side(opposite(side), [&](const auto& book) {
        Qty total = 0;
        for (auto level = book.begin(); level != book.end() && total < wanted; ++level) {
            if (!crosses(side, level->first, price)) break;
            total += level->second.total;
        }
        return total;
    });
}

inline Qty Engine::take(OrderId taker, Side side, std::optional<Price> limit, Qty qty, Events& out) {
    return on_side(opposite(side), [&](auto& book) {
        while (qty > 0 && !book.empty()) {
            auto level = book.begin();
            if (limit && !crosses(side, level->first, *limit)) break;
            Level& lv = level->second;
            while (qty > 0 && lv.head != kNil) {
                std::uint32_t idx   = lv.head;
                Node&         maker = nodes_[idx];
                Qty           fill  = std::min(qty, maker.qty);
                out.push_back(Trade{taker, maker.id, side, level->first, fill});
                qty -= fill;
                maker.qty -= fill;
                lv.total -= fill;
                if (maker.qty == 0) {
                    index_.erase(maker.id);
                    unlink(lv, idx);
                    release(idx);
                }
            }
            if (lv.head == kNil) book.erase(level);
        }
        return qty;
    });
}

inline void Engine::rest(Side side, OrderId id, Price price, Qty qty, bool post_only) {
    std::uint32_t idx = alloc();
    nodes_[idx] = Node{id, price, qty, kNil, kNil, side, post_only};
    index_.emplace(id, idx);
    on_side(side, [&](auto& book) {
        Level& lv = book[price];
        link_back(lv, idx);
        lv.total += qty;
    });
}

// Take a resting order out of the book entirely.
inline void Engine::remove(std::uint32_t idx) {
    const Node& node = nodes_[idx];
    index_.erase(node.id);
    on_side(node.side, [&](auto& book) {
        auto   level = book.find(node.price);
        Level& lv    = level->second;
        lv.total -= node.qty;
        unlink(lv, idx);
        if (lv.head == kNil) book.erase(level);
    });
    release(idx);
}

inline std::uint32_t Engine::alloc() {
    if (!free_.empty()) {
        std::uint32_t idx = free_.back();
        free_.pop_back();
        return idx;
    }
    nodes_.push_back(Node{});
    return static_cast<std::uint32_t>(nodes_.size() - 1);
}

inline void Engine::link_back(Level& lv, std::uint32_t idx) {
    nodes_[idx].prev = lv.tail;
    nodes_[idx].next = kNil;
    if (lv.tail == kNil) lv.head = idx;
    else nodes_[lv.tail].next = idx;
    lv.tail = idx;
}

inline void Engine::unlink(Level& lv, std::uint32_t idx) {
    Node& n = nodes_[idx];
    if (n.prev == kNil) lv.head = n.next;
    else nodes_[n.prev].next = n.next;
    if (n.next == kNil) lv.tail = n.prev;
    else nodes_[n.next].prev = n.prev;
    n.prev = n.next = kNil;
}

#define ME_INVARIANT(cond, what)                                                          \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "invariant broken: %s (%s:%d)\n", what, __FILE__, __LINE__); \
            std::abort();                                                                 \
        }                                                                                 \
    } while (0)

inline void Engine::check_invariants() const {
    if (!bids_.empty() && !asks_.empty())
        ME_INVARIANT(bids_.begin()->first < asks_.begin()->first, "book is crossed");

    std::size_t resting = 0;
    auto check_side = [&](const auto& book, Side side) {
        for (const auto& [price, lv] : book) {
            ME_INVARIANT(lv.head != kNil, "empty level left in book");
            Qty           sum  = 0;
            std::uint32_t prev = kNil;
            for (std::uint32_t i = lv.head; i != kNil; i = nodes_[i].next) {
                const Node& n = nodes_[i];
                ME_INVARIANT(n.prev == prev, "broken prev link");
                ME_INVARIANT(n.side == side && n.price == price, "order filed under wrong level");
                ME_INVARIANT(n.qty > 0, "zero-qty order resting");
                auto found = index_.find(n.id);
                ME_INVARIANT(found != index_.end() && found->second == i, "resting order missing from id index");
                sum += n.qty;
                prev = i;
                ++resting;
            }
            ME_INVARIANT(lv.tail == prev, "tail does not point at last order");
            ME_INVARIANT(lv.total == sum, "level total differs from sum of orders");
        }
    };
    check_side(bids_, Side::Buy);
    check_side(asks_, Side::Sell);
    ME_INVARIANT(resting == index_.size(), "id index holds orders that are not in the book");
}

#undef ME_INVARIANT

inline std::uint64_t Engine::state_hash() const {
    std::uint64_t h   = 1469598103934665603ULL;  // FNV-1a 64-bit offset basis
    auto          mix = [&h](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xff;
            h *= 1099511628211ULL;
        }
    };
    auto hash_side = [&](const auto& book, std::uint64_t tag) {
        mix(tag);
        for (const auto& [price, lv] : book) {
            mix(static_cast<std::uint64_t>(price));
            for (std::uint32_t i = lv.head; i != kNil; i = nodes_[i].next) {
                mix(nodes_[i].id);
                mix(nodes_[i].qty);
                mix(nodes_[i].post_only);
            }
            mix(~0ULL);  // level separator
        }
    };
    hash_side(bids_, 1);
    hash_side(asks_, 2);
    return h;
}

}  // namespace me
