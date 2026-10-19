#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "me/types.hpp"

namespace me {

enum class RejectReason : std::uint8_t { DuplicateId, ZeroQty, NonPositivePrice, UnknownId };

struct Ack {
    OrderId id;
    bool operator==(const Ack&) const = default;
};

// One fill between an incoming (taker) order and a resting (maker) order, at the maker's price.
struct Trade {
    OrderId taker;
    OrderId maker;
    Side    taker_side;
    Price   price;
    Qty     qty;
    bool operator==(const Trade&) const = default;
};

// Quantity that will never trade: removed by a cancel, or the unfilled rest of a market order.
struct Cancelled {
    OrderId id;
    Qty     qty;
    bool operator==(const Cancelled&) const = default;
};

struct Rejected {
    OrderId      id;
    RejectReason reason;
    bool operator==(const Rejected&) const = default;
};

using Event  = std::variant<Ack, Trade, Cancelled, Rejected>;
using Events = std::vector<Event>;

}  // namespace me
