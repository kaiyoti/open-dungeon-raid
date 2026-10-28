// Unit differential driver for SKILL activation (pairs with tools/emu/unit_skills.py, task V26).
// Input (stdin):
//   3 lines: base save hex (boards 0..2)
//   then one case per line:
//     board skill seed race classidx armour npokes  (c r type enemy atk def hp max cd) x npokes
// For each case: NewGame per board (seed board+1, mirrors the native NativeGame), DeserialiseState,
// clear the unserialised transient per-tile flags (HARNESS setup, not game logic: matched/inChain/
// abilityFired/falling must start clean on both sides), poke the listed tiles + player armour,
// replace the skill list with ONE Skill in slot 0 (type/skill, maxCharge 5, charge 5 = ready,
// rank 1 — harness input: a fresh game owns no skills, and the skill list is part of the save, so
// both sides must be given the same one), poke the ACTIVE class race, SetRandSeed(seed, all), then
// Game::ActivateSkill(0) — the rebuild's activation path, which is exactly what
// Interface::ProcessInput @ 0x49738:84-97 does on a skill-button release (CanActivate, Activate,
// charge reset, Dragonkin FireBreath) — DrainTokens (HARNESS: mirrors the native settle() token
// landing), and print "S <hex>" + "R <6 stream seeds>".
#include "dr/character_class.h"
#include "dr/game.h"
#include "dr/rng.h"
#include "dr/skill.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
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

// Poke a tile to an exact state (harness input, mirrors the native poke in unit_skills.py).
static void Poke(Tile &t, int type, int enemy, int atk, int def, int hp, int maxhp, int cd)
{
    t.matched = t.inChain = t.abilityFired = t.falling = false;
    t.type = static_cast<TileType>(type);
    t.enemyType = static_cast<EnemyType>(enemy);
    t.attack = atk; t.defence = def; t.health = hp; t.maxHealth = maxhp;
    t.countdown = cd;
    t.frozen = t.flaming = t.flamePulse = false; t.flameDamage = 0;
    t.broken = t.promoted = t.resurrectBlocked = false;
    t.magicSword = t.arrow = t.manaPotion = t.acidPotion = t.explosivePotion = false;
    t.poisonTurns = t.poison = t.poisonAddedThisTurn = 0;
}

int main()
{
    std::string line;
    std::vector<std::vector<uint8_t>> bases;
    for (int i = 0; i < 3; ++i) { if (!std::getline(std::cin, line)) return 1; bases.push_back(unhex(line)); }

    std::unique_ptr<Game> held;
    int lastBoard = -1;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::vector<int> v; int x;
        while (in >> x) v.push_back(x);
        if (v.size() < 7) continue;
        int board = v[0], skill = v[1], seed = v[2], race = v[3], classidx = v[4], armour = v[5], np = v[6];
        if (board != lastBoard) {
            // HARNESS: one FRESH Game per board, exactly like the native side keeps one independent
            // NativeGame per board (tools/emu/unit_skills.py) — turn/skill flags that are not in
            // the save must start at their constructor defaults, not carry over from the previous
            // board. NewGame is seeded with the board's base seed (BASE_SEEDS[b] = b+1, mirrors the
            // native) so the class roll in InitNewGame @ 0x2d7a4 matches — the class (mortal enemy,
            // perks) is NOT in the save and skills read it. Also allocates the 5 equipped items so
            // the item section aligns.
            held = std::make_unique<Game>();
            SetRandSeed(static_cast<uint32_t>(board + 1), RAND_ALL);
            held->NewGame(DIFF_NORMAL);
            lastBoard = board;
        }
        Game &g = *held;
        g.DeserialiseState(bases[board]);
        // HARNESS reset (not game logic): mirrors the native reset in unit_skills.py — the save
        // has no open menu and no in-flight tokens, and a menu left open by a previous Enchant
        // case would block DrainTokens (chain.cpp) exactly as Game::Process returns at +0x5ae4.
        g.menuPending = false; g.menuType = -1; g.tokenQueue.clear();
        for (int c = 0; c < BOARD_SIZE; ++c)
            for (int r = 0; r < BOARD_SIZE; ++r) {
                Tile &t = g.At(c, r);
                t.matched = t.inChain = t.abilityFired = t.falling = false;   // harness: clean base
            }
        for (int i = 0; i < np; ++i) {
            size_t o = 7 + static_cast<size_t>(i) * 9;
            if (o + 9 > v.size()) break;
            Poke(g.At(v[o], v[o + 1]), v[o + 2], v[o + 3], v[o + 4], v[o + 5], v[o + 6], v[o + 7], v[o + 8]);
        }
        g.armour = armour;                              // +0xc164 (Explosive Armour precondition)
        // HARNESS: exactly one ready active skill in slot 0 (the skill list is save state, and a
        // fresh game has none — see the header).
        g.skills.clear();
        auto s = std::make_unique<Skill>();
        s->game = &g; s->type = skill; s->maxCharge = 5; s->charge = 5; s->rank = 1;
        g.skills.push_back(std::move(s));
        g.classes[g.activeClass]->race = static_cast<RaceType>(race);   // +0x14dac+idx*0x184+8
        if (g.activeClass != classidx) std::printf("X class %d %d\n", g.activeClass, classidx);
        SetRandSeed(static_cast<uint32_t>(seed), RAND_ALL);
        if (std::getenv("DR_DEBUG")) {
            std::string bt;
            for (int r = 0; r < BOARD_SIZE; ++r)
                for (int c = 0; c < BOARD_SIZE; ++c)
                    bt += "0123456789abcdef"[static_cast<int>(g.At(c, r).type) & 15];
            std::fprintf(stderr, "REB pre  skill=%d q=%zu arm=%d up=%d xp=%d menu=%d t=%s\n",
                         skill, g.tokenQueue.size(), g.armour, g.upgradePoints, g.xp,
                         static_cast<int>(g.menuPending), bt.c_str());
        }
        g.ActivateSkill(0);                             // ProcessInput @0x49738:84-97
        if (std::getenv("DR_DEBUG")) {
            std::string bt;
            for (int r = 0; r < BOARD_SIZE; ++r)
                for (int c = 0; c < BOARD_SIZE; ++c)
                    bt += "0123456789abcdef"[static_cast<int>(g.At(c, r).type) & 15];
            std::fprintf(stderr, "REB post skill=%d q=%zu arm=%d up=%d xp=%d gold=%d menu=%d t=%s\n",
                         skill, g.tokenQueue.size(), g.armour, g.upgradePoints, g.xp, g.gold,
                         static_cast<int>(g.menuPending), bt.c_str());
        }
        g.DrainTokens();                                // HARNESS: mirrors the native settle()
        if (std::getenv("DR_DEBUG")) {
            std::string bt;
            for (int r = 0; r < BOARD_SIZE; ++r)
                for (int c = 0; c < BOARD_SIZE; ++c)
                    bt += "0123456789abcdef"[static_cast<int>(g.At(c, r).type) & 15];
            std::fprintf(stderr, "REB end  skill=%d q=%zu arm=%d up=%d xp=%d gold=%d t=%s\n",
                         skill, g.tokenQueue.size(), g.armour, g.upgradePoints, g.xp, g.gold,
                         bt.c_str());
        }
        std::printf("S ");
        for (uint8_t b : g.SerialiseState()) std::printf("%02x", b);
        std::printf("\nR");
        for (unsigned i = 0; i < 6; ++i) std::printf(" %u", GetRandSeed(i));
        std::printf("\n");
    }
    return 0;
}
