#pragma once

#include <cstdint>
#include <variant>

#include "me/types.hpp"

namespace me {

enum class OrderType : std::uint8_t {
    Limit,     // trade what crosses, rest the remainder
    Market,    // trade at any price, cancel the remainder
    IOC,       // immediate-or-cancel: trade what crosses, cancel the remainder
    FOK,       // fill-or-kill: trade the full size now, or nothing
    PostOnly,  // rest only; rejected if it would trade on arrival
};

struct NewOrder {
    OrderId   id;
    Side      side;
    OrderType type;
    Price     price;  // ignored for Market
    Qty       qty;
    bool operator==(const NewOrder&) const = default;
};

struct Cancel {
    OrderId id;
    bool operator==(const Cancel&) const = default;
};

// Change a resting order's price and/or open quantity. qty is the new open quantity, not a delta.
struct Modify {
    OrderId id;
    Price   price;
    Qty     qty;
    bool operator==(const Modify&) const = default;
};

using Command = std::variant<NewOrder, Cancel, Modify>;

}  // namespace me
