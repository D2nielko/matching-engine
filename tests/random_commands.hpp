#pragma once

#include <algorithm>
#include <cstdint>
#include <random>

#include "me/commands.hpp"

namespace me {

// Deterministic random command stream for differential and replay tests. Same seed, same stream.
// Prices cluster around a slowly drifting mid so orders cross often; cancels and modifies mostly
// target recent ids, so many hit live orders and some hit finished ones.
class RandomCommands {
public:
    explicit RandomCommands(std::uint64_t seed) : rng_(seed) {}

    Price mid() const { return mid_; }

    Command next() {
        if (pick(100) == 0) mid_ = std::max<Price>(100, mid_ + static_cast<Price>(pick(3)) - 1);
        std::uint64_t kind = pick(100);
        if (next_id_ > 1 && kind < 20) return Cancel{recent_id()};
        if (next_id_ > 1 && kind < 30) return Modify{recent_id(), near_mid(), size()};

        NewOrder o{};
        o.id    = (next_id_ > 1 && pick(200) == 0) ? recent_id() : next_id_++;  // occasional duplicate
        o.side  = pick(2) ? Side::Buy : Side::Sell;
        o.price = near_mid();
        o.qty   = size();
        std::uint64_t t = pick(100);
        o.type = t < 50 ? OrderType::Limit
               : t < 70 ? OrderType::PostOnly
               : t < 80 ? OrderType::IOC
               : t < 90 ? OrderType::FOK
                        : OrderType::Market;
        if (pick(1000) == 0) o.price = 0;  // invalid inputs, rarely
        return o;
    }

private:
    std::mt19937_64 rng_;
    Price           mid_     = 1000;
    OrderId         next_id_ = 1;

    std::uint64_t pick(std::uint64_t n) { return rng_() % n; }
    Price near_mid() { return mid_ + static_cast<Price>(pick(41)) - 20; }
    Qty size() { return pick(1000) == 0 ? 0 : 1 + pick(100); }
    OrderId recent_id() { return next_id_ - 1 - pick(std::min<OrderId>(next_id_ - 1, 200)); }
};

}  // namespace me
