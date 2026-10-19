#include <fstream>
#include <iterator>

#include "me/journal.hpp"
#include "random_commands.hpp"
#include "test_util.hpp"

using namespace me;
using journal::Status;

#ifdef ME_QUICK
constexpr Seq kCommands = 20'000;
#else
constexpr Seq kCommands = 200'000;
#endif

static const std::string kDir = "build/";

static std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

static void write_file(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// Writes kCommands random commands to `path` and returns them.
static std::vector<Command> write_random_journal(const std::string& path, std::uint64_t seed) {
    std::vector<Command> cmds;
    RandomCommands       gen(seed);
    journal::Writer      w(path);
    for (Seq i = 0; i < kCommands; ++i) {
        cmds.push_back(gen.next());
        w.append(cmds.back());
    }
    CHECK(w.close());
    return cmds;
}

static std::size_t record_offset(Seq seq) { return journal::kHeaderSize + (seq - 1) * journal::kRecordSize; }

// --- encoding ---

static void crc32_matches_standard_check_value() {
    const char* s = "123456789";
    CHECK(crc32(reinterpret_cast<const std::uint8_t*>(s), 9) == 0xCBF43926u);
}

static void every_command_kind_round_trips() {
    const Command cmds[] = {
        NewOrder{1, Side::Buy, OrderType::Limit, 631739, 100'000'000},
        NewOrder{2, Side::Sell, OrderType::Market, 0, 5},
        NewOrder{3, Side::Sell, OrderType::PostOnly, 1, ~0ULL},
        NewOrder{4, Side::Buy, OrderType::FOK, -7, 1},  // invalid for the engine, but must still round-trip
        Cancel{~0ULL},
        Modify{9, 631740, 42},
    };
    Seq seq = 1;
    for (const Command& c : cmds) {
        Seq     got_seq = 0;
        Command got;
        CHECK(journal::decode(journal::encode(seq, c), got_seq, got) == Status::Ok);
        CHECK(got_seq == seq);
        CHECK(got == c);
        ++seq;
    }
}

static void flipped_bit_fails_checksum() {
    auto rec = journal::encode(1, NewOrder{1, Side::Buy, OrderType::Limit, 100, 5});
    rec[20] ^= 0x01;
    Seq     seq;
    Command cmd;
    CHECK(journal::decode(rec, seq, cmd) == Status::BadChecksum);
}

// --- journal files ---

static void written_journal_reads_back_identically() {
    const std::string path = kDir + "jt_roundtrip.bin";
    auto              cmds = write_random_journal(path, 1);
    CHECK(read_file(path).size() == record_offset(kCommands + 1));

    journal::Reader r(path);
    Seq             seq;
    Command         cmd;
    std::size_t     i = 0, mismatches = 0;
    while (r.next(seq, cmd) == Status::Ok) mismatches += (seq != i + 1 || !(cmd == cmds[i])), ++i;
    CHECK(r.next(seq, cmd) == Status::End);
    CHECK(i == cmds.size());
    CHECK(mismatches == 0);
}

// --- replay ---

static void replay_twice_is_byte_identical_and_matches_live_run() {
    const std::string path = kDir + "jt_replay.bin";
    auto              cmds = write_random_journal(path, 2);

    // The run that produced the journal, done live without touching the file.
    Engine                    live;
    Events                    events;
    std::vector<std::uint8_t> live_out;
    for (Seq seq = 1; seq <= cmds.size(); ++seq) {
        events.clear();
        live.step(cmds[seq - 1], events);
        for (const Event& e : events) journal::encode_event(seq, e, live_out);
        if (seq % 10'000 == 0) journal::encode_checkpoint(seq, live.state_hash(), live_out);
    }

    ReplayResult a = replay(path);
    ReplayResult b = replay(path);
    CHECK(a.ok() && b.ok());
    CHECK(a.commands == kCommands);
    CHECK(a.output == b.output);  // replay twice: byte-identical
    CHECK(a.output == live_out);  // and identical to the original run
    CHECK(a.final_hash == live.state_hash());
    CHECK(a.checkpoints == b.checkpoints);
    CHECK(a.checkpoints.size() == kCommands / 10'000);
    write_file(kDir + "jt_replay_out_a.bin", a.output);
    write_file(kDir + "jt_replay_out_b.bin", b.output);
    CHECK(read_file(kDir + "jt_replay_out_a.bin") == read_file(kDir + "jt_replay_out_b.bin"));
}

static void state_hash_tracks_book_contents() {
    Engine e;
    auto   empty = e.state_hash();
    e.limit(1, Side::Buy, 100, 5);
    auto one = e.state_hash();
    CHECK(one != empty);
    e.modify(1, 100, 4);
    CHECK(e.state_hash() != one);
    e.cancel(1);
    CHECK(e.state_hash() == empty);

    // Same orders, different queue order: different hash.
    Engine x, y;
    x.limit(1, Side::Sell, 100, 5), x.limit(2, Side::Sell, 100, 5);
    y.limit(2, Side::Sell, 100, 5), y.limit(1, Side::Sell, 100, 5);
    CHECK(x.state_hash() != y.state_hash());
}

// --- corruption ---

static void corrupted_record_is_detected_and_replay_stops_before_it() {
    const std::string path = kDir + "jt_corrupt_src.bin";
    write_random_journal(path, 3);
    auto bytes = read_file(path);

    const Seq bad = 1234;
    bytes[record_offset(bad) + 20] ^= 0x40;  // one bit inside the price field
    write_file(kDir + "jt_corrupt.bin", bytes);

    ReplayResult r = replay(kDir + "jt_corrupt.bin");
    CHECK(r.status == Status::BadChecksum);
    CHECK(r.commands == bad - 1);
}

static void truncated_tail_is_detected() {
    const std::string path = kDir + "jt_trunc_src.bin";
    write_random_journal(path, 4);
    auto bytes = read_file(path);
    bytes.resize(bytes.size() - 7);  // torn final write
    write_file(kDir + "jt_trunc.bin", bytes);

    ReplayResult r = replay(kDir + "jt_trunc.bin");
    CHECK(r.status == Status::Truncated);
    CHECK(r.commands == kCommands - 1);
}

static void missing_or_duplicated_record_breaks_sequence() {
    const std::string path = kDir + "jt_seq_src.bin";
    write_random_journal(path, 5);
    auto      bytes = read_file(path);
    const Seq k     = 500;

    auto dropped = bytes;
    dropped.erase(dropped.begin() + record_offset(k), dropped.begin() + record_offset(k + 1));
    write_file(kDir + "jt_dropped.bin", dropped);
    ReplayResult r1 = replay(kDir + "jt_dropped.bin");
    CHECK(r1.status == Status::BadSequence);
    CHECK(r1.commands == k - 1);

    auto duplicated = bytes;
    duplicated.insert(duplicated.begin() + record_offset(k + 1), bytes.begin() + record_offset(k),
                      bytes.begin() + record_offset(k + 1));
    write_file(kDir + "jt_duplicated.bin", duplicated);
    ReplayResult r2 = replay(kDir + "jt_duplicated.bin");
    CHECK(r2.status == Status::BadSequence);
    CHECK(r2.commands == k);
}

static void bad_header_and_missing_file_are_rejected() {
    const std::string path = kDir + "jt_header_src.bin";
    write_random_journal(path, 6);
    auto bytes = read_file(path);
    bytes[0]   = 'X';
    write_file(kDir + "jt_bad_header.bin", bytes);
    CHECK(replay(kDir + "jt_bad_header.bin").status == Status::BadHeader);
    CHECK(replay(kDir + "jt_bad_header.bin").commands == 0);
    CHECK(replay(kDir + "does_not_exist.bin").status == Status::IoError);
}

static void empty_journal_replays_to_empty_book() {
    const std::string path = kDir + "jt_empty.bin";
    { journal::Writer w(path); }
    ReplayResult r = replay(path);
    CHECK(r.ok());
    CHECK(r.commands == 0);
    CHECK(r.final_hash == Engine{}.state_hash());
}

int main() {
    struct Case {
        const char* name;
        void (*fn)();
    };
    const Case cases[] = {
        {"crc32_matches_standard_check_value", crc32_matches_standard_check_value},
        {"every_command_kind_round_trips", every_command_kind_round_trips},
        {"flipped_bit_fails_checksum", flipped_bit_fails_checksum},
        {"written_journal_reads_back_identically", written_journal_reads_back_identically},
        {"replay_twice_is_byte_identical_and_matches_live_run", replay_twice_is_byte_identical_and_matches_live_run},
        {"state_hash_tracks_book_contents", state_hash_tracks_book_contents},
        {"corrupted_record_is_detected_and_replay_stops_before_it", corrupted_record_is_detected_and_replay_stops_before_it},
        {"truncated_tail_is_detected", truncated_tail_is_detected},
        {"missing_or_duplicated_record_breaks_sequence", missing_or_duplicated_record_breaks_sequence},
        {"bad_header_and_missing_file_are_rejected", bad_header_and_missing_file_are_rejected},
        {"empty_journal_replays_to_empty_book", empty_journal_replays_to_empty_book},
    };
    for (const Case& c : cases) {
        int before = failures;
        c.fn();
        std::printf("%s %s\n", failures == before ? "PASS" : "FAIL", c.name);
    }
    std::printf("%zu cases, %d failed checks\n", std::size(cases), failures);
    return failures == 0 ? 0 : 1;
}
