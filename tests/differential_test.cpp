// Feeds the same random command stream to ReferenceBook and Engine and requires identical events at
// every step. Any divergence is a bug in one of them; the failing step is printed so it can be turned
// into a hand-written case in book_test.cpp.

#include <chrono>

#include "me/engine.hpp"
#include "me/reference_book.hpp"
#include "random_commands.hpp"
#include "test_util.hpp"

using namespace me;

#ifdef ME_QUICK
constexpr std::uint64_t kSteps = 20'000;  // sanitizer builds are slow
#else
constexpr std::uint64_t kSteps = 1'000'000;
#endif
constexpr std::uint64_t kSeeds        = 10;
constexpr std::uint64_t kCompareEvery = 1'000;

static bool same_book(const ReferenceBook& r, const Engine& e, Price mid) {
    if (r.best_bid() != e.best_bid() || r.best_ask() != e.best_ask()) return false;
    if (r.order_count() != e.order_count()) return false;
    for (Price p = mid - 40; p <= mid + 40; ++p)
        if (r.depth(Side::Buy, p) != e.depth(Side::Buy, p) || r.depth(Side::Sell, p) != e.depth(Side::Sell, p))
            return false;
    return true;
}

static bool run_seed(std::uint64_t seed) {
    RandomCommands gen(seed);
    ReferenceBook  ref;
    Engine         eng;
    Events         out;
    std::uint64_t  trades = 0, rejects = 0;
    auto           t0     = std::chrono::steady_clock::now();

    for (std::uint64_t step = 0; step < kSteps; ++step) {
        Command cmd      = gen.next();
        Events  expected = ref.apply(cmd);
        out.clear();
        eng.step(cmd, out);
        if (out != expected) {
            std::fprintf(stderr, "  seed %llu step %llu: %s\n    reference: %s\n    engine:    %s\n",
                         (unsigned long long)seed, (unsigned long long)step, to_string(cmd).c_str(),
                         to_string(expected).c_str(), to_string(out).c_str());
            ++failures;
            return false;
        }
        for (const Event& ev : out) {
            trades += std::holds_alternative<Trade>(ev);
            rejects += std::holds_alternative<Rejected>(ev);
        }
        if (step % kCompareEvery == 0) {
            eng.check_invariants();
            if (!same_book(ref, eng, gen.mid())) {
                std::fprintf(stderr, "  seed %llu step %llu: books differ after identical events\n",
                             (unsigned long long)seed, (unsigned long long)step);
                ++failures;
                return false;
            }
        }
    }
    eng.check_invariants();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("PASS seed %llu: %llu commands, %llu trades, %llu rejects, %zu resting at end (%.0f ms)\n",
                (unsigned long long)seed, (unsigned long long)kSteps, (unsigned long long)trades,
                (unsigned long long)rejects, eng.order_count(), ms);
    return true;
}

int main() {
    std::uint64_t passed = 0;
    for (std::uint64_t seed = 1; seed <= kSeeds; ++seed) passed += run_seed(seed);
    std::printf("%llu/%llu seeds identical\n", (unsigned long long)passed, (unsigned long long)kSeeds);
    return failures == 0 ? 0 : 1;
}
