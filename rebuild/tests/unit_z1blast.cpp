// Z1 unit differential driver (pairs with tools/emu/unit_z1blast.py): explosive-potion 3x3 blast.
// Pins the measured rule: the blast clears NORMAL skulls only; special enemies (enemyType != 0)
// are spared (native MatchTile @0x2ac40 potion blast: `*(int*)pGVar8 == 1` then the neighbour's
// +0x25c (enemy kind) gates the clear; emu measurement tasks/Z1: special kind=3 survived,
// normal kind=0 was marked). Input (stdin):
//   1 line: base save hex
//   then one case per line: seed pc pr npokes  (c r type enemy atk def hp max cd expl) x npokes
// For each case: DeserialiseState(base), clear transient flags (harness), poke, SetRandSeed,
// MatchTile(At(pc,pr), 5), MoveMatchedTiles, print save hex. Output: one "S <hex>" per case.
#include "dr/game.h"
#include "dr/rng.h"
#include <cstdint>
#include <cstdio>
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

static void Poke(Game &g, int c, int r, int type, int enemy, int atk, int def, int hp, int maxhp,
                 int cd, int expl)
{
    Tile &t = g.At(c, r);
    t.matched = t.inChain = t.abilityFired = t.falling = false;
    t.type = static_cast<TileType>(type);
    t.enemyType = static_cast<EnemyType>(enemy);
    t.attack = atk; t.defence = def; t.health = hp; t.maxHealth = maxhp;
    t.countdown = cd;
    t.frozen = t.flaming = false; t.flameDamage = 0;
    t.broken = t.promoted = t.resurrectBlocked = false;
    t.magicSword = t.arrow = t.manaPotion = t.acidPotion = false;
    t.explosivePotion = expl != 0;
    t.poisonTurns = t.poison = t.poisonAddedThisTurn = 0;
}

int main()
{
    std::string line;
    if (!std::getline(std::cin, line)) return 1;
    std::vector<uint8_t> base = unhex(line);

    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::vector<int> v; int x;
        while (in >> x) v.push_back(x);
        if (v.size() < 4) continue;
        int seed = v[0], pc = v[1], pr = v[2], np = v[3];
        // HARNESS: fresh Game + NewGame seeded with the case seed (mirrors NativeGame(seed) on the
        // native side; allocates the 5 equipped items so the item section aligns — unit_skills.cpp).
        Game g;
        SetRandSeed(static_cast<uint32_t>(seed), RAND_ALL);
        g.NewGame(DIFF_NORMAL);
        g.DeserialiseState(base);
        for (int c = 0; c < BOARD_SIZE; ++c)
            for (int r = 0; r < BOARD_SIZE; ++r) {
                Tile &t = g.At(c, r);
                t.matched = t.inChain = t.abilityFired = t.falling = false;
            }
        for (int i = 0; i < np; ++i) {
            const int *p = &v[4 + i * 10];
            Poke(g, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9]);
        }
        SetRandSeed(static_cast<uint32_t>(seed), RAND_ALL);
        g.MatchTile(g.At(pc, pr), static_cast<TileType>(5));   // decomp passes 5 = use own type
        g.MoveMatchedTiles();
        std::printf("S ");
        for (uint8_t b : g.SerialiseState()) std::printf("%02x", b);
        std::printf("\n");
    }
    return 0;
}
