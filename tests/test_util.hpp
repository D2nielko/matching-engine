#pragma once

#include <cstdio>
#include <string>

#include "me/commands.hpp"
#include "me/events.hpp"

inline int failures = 0;

// Variadic so braced lists with commas (Events{Rejected{1, ...}}) can be passed without extra parens.
#define CHECK(...)                                                                            \
    do {                                                                                      \
        if (!(__VA_ARGS__)) {                                                                 \
            std::fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #__VA_ARGS__); \
            ++failures;                                                                       \
        }                                                                                     \
    } while (0)

namespace me {

inline const char* name(Side s) { return s == Side::Buy ? "Buy" : "Sell"; }

inline const char* name(OrderType t) {
    switch (t) {
        case OrderType::Limit: return "Limit";
        case OrderType::Market: return "Market";
        case OrderType::IOC: return "IOC";
        case OrderType::FOK: return "FOK";
        case OrderType::PostOnly: return "PostOnly";
    }
    return "?";
}

inline const char* name(RejectReason r) {
    switch (r) {
        case RejectReason::DuplicateId: return "DuplicateId";
        case RejectReason::ZeroQty: return "ZeroQty";
        case RejectReason::NonPositivePrice: return "NonPositivePrice";
        case RejectReason::UnknownId: return "UnknownId";
        case RejectReason::WouldTrade: return "WouldTrade";
    }
    return "?";
}

inline std::string to_string(const Command& c) {
    char buf[160];
    if (auto* o = std::get_if<NewOrder>(&c))
        std::snprintf(buf, sizeof buf, "New{id=%llu %s %s px=%lld qty=%llu}", (unsigned long long)o->id,
                      name(o->side), name(o->type), (long long)o->price, (unsigned long long)o->qty);
    else if (auto* x = std::get_if<Cancel>(&c))
        std::snprintf(buf, sizeof buf, "Cancel{id=%llu}", (unsigned long long)x->id);
    else {
        auto& m = std::get<Modify>(c);
        std::snprintf(buf, sizeof buf, "Modify{id=%llu px=%lld qty=%llu}", (unsigned long long)m.id,
                      (long long)m.price, (unsigned long long)m.qty);
    }
    return buf;
}

inline std::string to_string(const Event& e) {
    char buf[160];
    if (auto* a = std::get_if<Ack>(&e))
        std::snprintf(buf, sizeof buf, "Ack{%llu}", (unsigned long long)a->id);
    else if (auto* t = std::get_if<Trade>(&e))
        std::snprintf(buf, sizeof buf, "Trade{taker=%llu maker=%llu %s px=%lld qty=%llu}",
                      (unsigned long long)t->taker, (unsigned long long)t->maker, name(t->taker_side),
                      (long long)t->price, (unsigned long long)t->qty);
    else if (auto* c = std::get_if<Cancelled>(&e))
        std::snprintf(buf, sizeof buf, "Cancelled{%llu qty=%llu}", (unsigned long long)c->id,
                      (unsigned long long)c->qty);
    else if (auto* m = std::get_if<Modified>(&e))
        std::snprintf(buf, sizeof buf, "Modified{%llu px=%lld qty=%llu}", (unsigned long long)m->id,
                      (long long)m->price, (unsigned long long)m->qty);
    else {
        auto& r = std::get<Rejected>(e);
        std::snprintf(buf, sizeof buf, "Rejected{%llu %s}", (unsigned long long)r.id, name(r.reason));
    }
    return buf;
}

inline std::string to_string(const Events& es) {
    std::string s = "[";
    for (std::size_t i = 0; i < es.size(); ++i) s += (i ? ", " : "") + to_string(es[i]);
    return s + "]";
}

}  // namespace me
