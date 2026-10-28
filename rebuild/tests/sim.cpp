// Headless simulation test for the reconstruction.
//   - plays games with a random legal-move policy (fixed seeds)
//   - every turn: save -> load into a fresh Game -> save again; bytes must be identical
//   - determinism: replaying a seed must give the same final save bytes
#include "dr/game.h"
#include "dr/rng.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace dr;

#include "policy.h"
using namespace drtest;

struct Result { int turns; bool over; std::vector<uint8_t> finalSave; int roundTripFailures; int stuck; };

static Result Play(uint32_t seed, int maxTurns)
{
    Game g; g.seedOverride = seed;
    g.NewGame(DIFF_NORMAL);
    std::mt19937 pick(seed);
    Result r{0, false, {}, 0, 0};
    for (int i = 0; i < maxTurns && !g.gameOver; ++i) {
        auto move = FindMove(g, pick);
        if (move.empty() && !AnyMove(g, move)) {
            r.stuck++;
            std::printf("  seed %u turn %d: board has NO legal move (exhaustive search)\n", seed, r.turns);
            break;
        }
        if (!g.PlayMove(move)) { r.stuck++; break; }
        // resolve every reward menu the move opened (same rule as drcli: first option; level-up 0+1)
        while (g.menuPending) g.ChooseMenu(0, g.menuType == MENU_LEVEL_UP ? 1 : -1);
        r.turns++;
        auto a = g.SerialiseState();
        Game h; h.seedOverride = seed; h.NewGame(DIFF_NORMAL);
        // restore the game's RNG afterwards: loading sets streams 1..5 from the save (same values)
        bool loaded = h.DeserialiseState(a);
        auto b = h.SerialiseState();
        if (!loaded || a != b) {
            if (r.roundTripFailures++ == 0) {
                size_t k = 0; while (k < a.size() && k < b.size() && a[k] == b[k]) ++k;
                std::printf("  seed %u turn %d: round-trip mismatch at byte %zu (sizes %zu/%zu, loaded=%d)\n",
                            seed, r.turns, k, a.size(), b.size(), loaded);
            }
        }
    }
    r.over = g.gameOver; r.finalSave = g.SerialiseState();
    return r;
}

int main(int argc, char **argv)
{
    int games = argc > 1 ? std::atoi(argv[1]) : 20, maxTurns = argc > 2 ? std::atoi(argv[2]) : 300;
    int failures = 0, totalTurns = 0, deaths = 0, rtFail = 0, stuck = 0, nondet = 0;
    for (int s = 1; s <= games; ++s) {
        Result a = Play(1000u + s, maxTurns);
        Result b = Play(1000u + s, maxTurns);
        bool det = a.finalSave == b.finalSave && a.turns == b.turns;
        if (!det) { nondet++; std::printf("  seed %u: NOT deterministic\n", 1000u + s); }
        totalTurns += a.turns; deaths += a.over; rtFail += a.roundTripFailures; stuck += a.stuck;
        std::printf("seed %u: %3d turns, %s%s\n", 1000u + s, a.turns, a.over ? "died" : "alive",
                    a.roundTripFailures ? ", ROUND-TRIP FAIL" : "");
    }
    failures = rtFail + nondet;
    std::printf("--- %d games, %d turns total, %d deaths, %d stuck, %d round-trip failures, %d nondeterministic\n",
                games, totalTurns, deaths, stuck, rtFail, nondet);
    return failures ? 1 : 0;
}
