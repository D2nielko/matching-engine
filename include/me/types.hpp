#pragma once

#include <cstdint>

namespace me {

using OrderId = std::uint64_t;
using Price   = std::int64_t;   // integer ticks; never floating point
using Qty     = std::uint64_t;  // integer lots (1e-8 BTC on Kraken); 32 bits caps out near 42.9 BTC
using Seq     = std::uint64_t;  // monotonically increasing message sequence number

enum class Side : std::uint8_t { Buy, Sell };

}  // namespace me
