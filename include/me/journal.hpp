#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "me/commands.hpp"
#include "me/crc32.hpp"
#include "me/engine.hpp"
#include "me/events.hpp"

// Binary journal of commands, and replay of a journal through the engine.
//
// Journal file layout (all integers little-endian):
//
//   header  16 bytes   "MEJOURN1" | u32 version (1) | u32 record size (40)
//   record  40 bytes   u64 seq | u64 id | i64 price | u64 qty | u8 kind | u8 side | u8 type | u8 0 | u32 crc32
//
// seq starts at 1 and increases by exactly 1 per record. crc32 covers the first 36 bytes of the
// record. Fixed-width records make a torn or truncated tail easy to spot and let a reader seek to
// record n directly.

namespace me {

namespace journal {

inline constexpr char          kMagic[8]    = {'M', 'E', 'J', 'O', 'U', 'R', 'N', '1'};
inline constexpr std::uint32_t kVersion     = 1;
inline constexpr std::size_t   kHeaderSize  = 16;
inline constexpr std::size_t   kRecordSize  = 40;
inline constexpr std::size_t   kEventSize   = 48;

enum Kind : std::uint8_t { kNew = 1, kCancel = 2, kModify = 3 };

using Record = std::array<std::uint8_t, kRecordSize>;

inline void put_u32(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline void put_u64(std::uint8_t* p, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
inline std::uint32_t get_u32(const std::uint8_t* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    return v;
}
inline std::uint64_t get_u64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return v;
}

inline Record encode(Seq seq, const Command& cmd) {
    Record r{};
    std::uint8_t kind = 0, side = 0, type = 0;
    OrderId      id    = 0;
    Price        price = 0;
    Qty          qty   = 0;
    if (auto* o = std::get_if<NewOrder>(&cmd)) {
        kind = kNew, side = static_cast<std::uint8_t>(o->side), type = static_cast<std::uint8_t>(o->type);
        id = o->id, price = o->price, qty = o->qty;
    } else if (auto* c = std::get_if<Cancel>(&cmd)) {
        kind = kCancel, id = c->id;
    } else {
        const auto& m = std::get<Modify>(cmd);
        kind = kModify, id = m.id, price = m.price, qty = m.qty;
    }
    put_u64(&r[0], seq);
    put_u64(&r[8], id);
    put_u64(&r[16], static_cast<std::uint64_t>(price));
    put_u64(&r[24], qty);
    r[32] = kind;
    r[33] = side;
    r[34] = type;
    put_u32(&r[36], crc32(r.data(), 36));
    return r;
}

enum class Status { Ok, End, BadHeader, Truncated, BadChecksum, BadSequence, BadRecord, IoError };

inline const char* to_string(Status s) {
    switch (s) {
        case Status::Ok: return "ok";
        case Status::End: return "end";
        case Status::BadHeader: return "bad header";
        case Status::Truncated: return "truncated record";
        case Status::BadChecksum: return "bad checksum";
        case Status::BadSequence: return "bad sequence number";
        case Status::BadRecord: return "bad record contents";
        case Status::IoError: return "I/O error";
    }
    return "?";
}

// Checks the checksum and field ranges. Does not check seq; the reader does that.
inline Status decode(const Record& r, Seq& seq, Command& cmd) {
    if (get_u32(&r[36]) != crc32(r.data(), 36)) return Status::BadChecksum;
    seq           = get_u64(&r[0]);
    OrderId id    = get_u64(&r[8]);
    Price   price = static_cast<Price>(get_u64(&r[16]));
    Qty     qty   = get_u64(&r[24]);
    std::uint8_t kind = r[32], side = r[33], type = r[34];
    if (r[35] != 0 || side > 1 || type > static_cast<std::uint8_t>(OrderType::PostOnly)) return Status::BadRecord;
    switch (kind) {
        case kNew: cmd = NewOrder{id, static_cast<Side>(side), static_cast<OrderType>(type), price, qty}; break;
        case kCancel: cmd = Cancel{id}; break;
        case kModify: cmd = Modify{id, price, qty}; break;
        default: return Status::BadRecord;
    }
    return Status::Ok;
}

class Writer {
public:
    explicit Writer(const std::string& path) : f_(std::fopen(path.c_str(), "wb")) {
        if (!f_) return;
        std::uint8_t h[kHeaderSize]{};
        std::memcpy(h, kMagic, 8);
        put_u32(h + 8, kVersion);
        put_u32(h + 12, kRecordSize);
        ok_ = std::fwrite(h, 1, kHeaderSize, f_) == kHeaderSize;
    }
    ~Writer() { close(); }
    Writer(const Writer&)            = delete;
    Writer& operator=(const Writer&) = delete;

    bool ok() const { return ok_; }

    // Appends one command and returns its sequence number (0 on failure).
    Seq append(const Command& cmd) {
        if (!ok_) return 0;
        Record r = encode(next_seq_, cmd);
        if (std::fwrite(r.data(), 1, r.size(), f_) != r.size()) {
            ok_ = false;
            return 0;
        }
        return next_seq_++;
    }

    // Flushes to the OS. Not an fsync: a power cut can still lose the tail, which the reader detects.
    bool flush() { return ok_ && std::fflush(f_) == 0; }

    bool close() {
        if (!f_) return ok_;
        ok_ = (std::fclose(f_) == 0) && ok_;
        f_  = nullptr;
        return ok_;
    }

private:
    std::FILE* f_;
    bool       ok_      = false;
    Seq        next_seq_ = 1;
};

class Reader {
public:
    explicit Reader(const std::string& path) : f_(std::fopen(path.c_str(), "rb")) {
        if (!f_) {
            status_ = Status::IoError;
            return;
        }
        std::uint8_t h[kHeaderSize];
        if (std::fread(h, 1, kHeaderSize, f_) != kHeaderSize || std::memcmp(h, kMagic, 8) != 0 ||
            get_u32(h + 8) != kVersion || get_u32(h + 12) != kRecordSize)
            status_ = Status::BadHeader;
    }
    ~Reader() {
        if (f_) std::fclose(f_);
    }
    Reader(const Reader&)            = delete;
    Reader& operator=(const Reader&) = delete;

    // Reads the next command. Returns Ok, End at a clean end of file, or the first error. After an
    // error every later call returns the same error.
    Status next(Seq& seq, Command& cmd) {
        if (status_ != Status::Ok) return status_;
        Record      r;
        std::size_t n = std::fread(r.data(), 1, r.size(), f_);
        if (n == 0) return status_ = (std::ferror(f_) ? Status::IoError : Status::End);
        if (n != r.size()) return status_ = Status::Truncated;
        if (Status s = decode(r, seq, cmd); s != Status::Ok) return status_ = s;
        if (seq != expected_) return status_ = Status::BadSequence;
        ++expected_;
        return Status::Ok;
    }

    // Sequence number of the last good record (0 if none).
    Seq last_good() const { return expected_ - 1; }

private:
    std::FILE* f_;
    Status     status_   = Status::Ok;
    Seq        expected_ = 1;
};

// Output event record, 48 bytes:
//   u64 seq | u8 tag | u8 side | u8 reason | 5 zero bytes | u64 a | u64 b | i64 price | u64 qty
// a/b are ids (Trade: taker/maker). A checkpoint (tag 0xFF) carries the engine state hash in a.
inline void encode_event(Seq seq, const Event& e, std::vector<std::uint8_t>& out) {
    std::uint8_t r[kEventSize]{};
    std::uint8_t tag = static_cast<std::uint8_t>(e.index() + 1), side = 0, reason = 0;
    std::uint64_t a = 0, b = 0, qty = 0;
    Price         price = 0;
    if (auto* x = std::get_if<Ack>(&e)) a = x->id;
    else if (auto* t = std::get_if<Trade>(&e)) a = t->taker, b = t->maker, side = static_cast<std::uint8_t>(t->taker_side), price = t->price, qty = t->qty;
    else if (auto* c = std::get_if<Cancelled>(&e)) a = c->id, qty = c->qty;
    else if (auto* m = std::get_if<Modified>(&e)) a = m->id, price = m->price, qty = m->qty;
    else {
        const auto& j = std::get<Rejected>(e);
        a = j.id, reason = static_cast<std::uint8_t>(j.reason);
    }
    put_u64(r, seq);
    r[8]  = tag;
    r[9]  = side;
    r[10] = reason;
    put_u64(r + 16, a);
    put_u64(r + 24, b);
    put_u64(r + 32, static_cast<std::uint64_t>(price));
    put_u64(r + 40, qty);
    out.insert(out.end(), r, r + kEventSize);
}

inline void encode_checkpoint(Seq seq, std::uint64_t state_hash, std::vector<std::uint8_t>& out) {
    std::uint8_t r[kEventSize]{};
    put_u64(r, seq);
    r[8] = 0xFF;
    put_u64(r + 16, state_hash);
    out.insert(out.end(), r, r + kEventSize);
}

}  // namespace journal

struct Checkpoint {
    Seq           seq;
    std::uint64_t state_hash;
    bool operator==(const Checkpoint&) const = default;
};

struct ReplayResult {
    journal::Status           status = journal::Status::Ok;  // End on success
    Seq                       commands = 0;                   // commands applied
    std::uint64_t             events   = 0;
    std::uint64_t             final_hash = 0;
    std::vector<Checkpoint>   checkpoints;                    // every `hash_every` commands
    std::vector<std::uint8_t> output;                         // encoded events and checkpoints

    bool ok() const { return status == journal::Status::End; }
};

// Replays a journal into a fresh engine. Stops at the first bad record; everything before it is
// applied and reported, so the caller can see exactly where the log went wrong.
inline ReplayResult replay(const std::string& path, std::uint64_t hash_every = 10'000) {
    ReplayResult    res;
    journal::Reader in(path);
    Engine          engine;
    Events          events;
    Seq             seq;
    Command         cmd;
    while ((res.status = in.next(seq, cmd)) == journal::Status::Ok) {
        events.clear();
        engine.step(cmd, events);
        for (const Event& e : events) journal::encode_event(seq, e, res.output);
        res.events += events.size();
        res.commands = seq;
        if (hash_every && seq % hash_every == 0) {
            Checkpoint cp{seq, engine.state_hash()};
            res.checkpoints.push_back(cp);
            journal::encode_checkpoint(seq, cp.state_hash, res.output);
        }
    }
    res.final_hash = engine.state_hash();
    return res;
}

}  // namespace me
