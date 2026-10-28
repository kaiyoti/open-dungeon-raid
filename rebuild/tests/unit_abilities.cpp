// Unit differential driver for enemy abilities (pairs with tools/emu/unit_abilities.py, task V13).
// Input (stdin):
//   5 lines: base save hex (boards 0..4)
//   then one case per line:
//     board ability seed ec er npokes  (c r type enemy atk def hp max cd) x npokes
// For each case: DeserialiseState(base), clear transient per-tile flags (HARNESS setup, not game
// logic: matched/inChain/abilityFired/falling are not serialised and must start clean on both sides),
// poke the listed tiles, SetRandSeed(seed, all), call Tile::ActivateAbility on At(ec,er), print the
// save hex. Output: one "S <hex>" line per case.
#include "dr/game.h"
#include "dr/rng.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace dr;

static std::vector<uint8_t> unhex(const std::string &s)
{
    std::vector<uint8_t> b;
    auto d = [](char c) -> int {
        return (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : c - 'A' + 10;
    };
    for (size_t i = 0; i + 1 < s.size(); i += 2) b.push_back(static_cast<uint8_t>(d(s[i]) * 16 + d(s[i + 1])));
    return b;
}

// Poke a tile to an exact state (harness input, mirrors the native poke in unit_abilities.py).
static void Poke(Tile &t, int type, int enemy, int atk, int def, int hp, int maxhp, int cd)
{
    t.matched = t.inChain = t.abilityFired = t.falling = false;
    t.type = static_cast<TileType>(type);
    t.enemyType = static_cast<EnemyType>(enemy);
    t.attack = atk; t.defence = def; t.health = hp; t.maxHealth = maxhp;
    t.countdown = cd;
    t.frozen = t.flaming = false; t.flameDamage = 0;
    t.broken = t.promoted = t.resurrectBlocked = false;
    t.magicSword = t.arrow = t.manaPotion = t.acidPotion = t.explosivePotion = false;
    t.poisonTurns = t.poison = t.poisonAddedThisTurn = 0;
}

int main()
{
    std::string line;
    std::vector<std::vector<uint8_t>> bases;
    for (int i = 0; i < 5; ++i) { if (!std::getline(std::cin, line)) return 1; bases.push_back(unhex(line)); }

    Game g;
    int lastBoard = -1;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::vector<int> v; int x;
        while (in >> x) v.push_back(x);
        if (v.size() < 6) continue;
        int board = v[0], ability = v[1], seed = v[2], ec = v[3], er = v[4], np = v[5];
        if (board != lastBoard) {
            // HARNESS: NewGame per board, seeded with the board's base seed (BASE_SEEDS[b] = b+1,
            // mirrors tools/emu/unit_abilities.py) so the class roll in InitNewGame @ 0x2d7a4
            // matches the native NativeGame(BASE_SEEDS[b]) — the class (mortal enemy, perks) is
            // NOT in the save, and abilities read it (TeleportEnemy +0xc381, StabAttack, StealGold,
            // RandomiseTile perks). Also allocates the 5 equipped items so the item section aligns.
            SetRandSeed(static_cast<uint32_t>(board + 1), RAND_ALL);
            g.NewGame(DIFF_NORMAL);
            lastBoard = board;
        }
        g.DeserialiseState(bases[board]);
        for (int c = 0; c < BOARD_SIZE; ++c)
            for (int r = 0; r < BOARD_SIZE; ++r) {
                Tile &t = g.At(c, r);
                t.matched = t.inChain = t.abilityFired = t.falling = false;   // harness: clean base
            }
        for (int i = 0; i < np; ++i) {
            size_t o = 6 + static_cast<size_t>(i) * 9;
            if (o + 9 > v.size()) break;
            Poke(g.At(v[o], v[o + 1]), v[o + 2], v[o + 3], v[o + 4], v[o + 5], v[o + 6], v[o + 7], v[o + 8]);
        }
        SetRandSeed(static_cast<uint32_t>(seed), RAND_ALL);
        g.At(ec, er).ActivateAbility();
        g.DrainTokens();   // HARNESS: mirrors the native settle() (Game::Process token loop lands
                           // queued tokens via AddResource; no gravity, no turn advance).
        std::printf("S ");
        for (uint8_t b : g.SerialiseState()) std::printf("%02x", b);
        std::printf("\n");
    }
    return 0;
}
