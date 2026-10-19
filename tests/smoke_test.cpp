#include <type_traits>

#include "me/types.hpp"

static_assert(std::is_integral_v<me::Price>, "prices are integer ticks");
static_assert(std::is_signed_v<me::Price>, "price differences may be negative");

int main() { return 0; }
