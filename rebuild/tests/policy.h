// Random legal-move policy shared by sim.cpp and drcli.cpp.
#pragma once
#include "dr/game.h"
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

namespace drtest {
using namespace dr;

// Exhaustive fallback: depth-first search for any legal chain of exactly 3 tiles.
inline bool AnyMove(Game &g, std::vector<Game::Cell> &out)
{
    for (int c0 = 0; c0 < 6; ++c0) for (int r0 = 0; r0 < 6; ++r0)
    for (int d1 = 0; d1 < 9; ++d1) for (int d2 = 0; d2 < 9; ++d2) {
        Game::Cell a{c0, r0}, b{c0 + d1 % 3 - 1, r0 + d1 / 3 - 1};
        Game::Cell c{b.col + d2 % 3 - 1, b.row + d2 / 3 - 1};
        std::vector<Game::Cell> p{a, b, c};
        if (g.IsValidMove(p)) { out = p; return true; }
    }
    return false;
}

// Grow a legal chain, preferring the tiles that keep the player alive: skulls in a chain get
// attacked (CHAIN.md: dmg = chain attack power per skull tile) so killing them cuts incoming
// end-of-turn damage and pays XP; swords are attack power; potions heal. Deterministic: the
// scan order is fixed, `pick` is unused (kept for the sim.cpp/drcli.cpp signature).
inline int ScoreMove(Game &g, const std::vector<Game::Cell> &p)
{
    int s = 0;
    for (auto &c : p) {
        switch (g.At(c.col, c.row).type) {
        case TILE_SKULL:  s += 6; break;
        case TILE_POTION: s += 3; break;
        case TILE_COIN: case TILE_SHIELD: s += 2; break;
        case TILE_SWORD:  s += 2; break;
        default: break;
        }
    }
    return s + static_cast<int>(p.size());
}

inline std::vector<Game::Cell> FindMove(Game &g, std::mt19937 &pick)
{
    (void)pick;
    std::vector<Game::Cell> best;
    int bestScore = -1000;
    for (int c0 = 0; c0 < 6; ++c0) for (int r0 = 0; r0 < 6; ++r0)
    for (int d1 = 0; d1 < 9; ++d1) for (int d2 = 0; d2 < 9; ++d2) {
        Game::Cell a{c0, r0}, b{c0 + d1 % 3 - 1, r0 + d1 / 3 - 1};
        Game::Cell c{b.col + d2 % 3 - 1, b.row + d2 / 3 - 1};
        std::vector<Game::Cell> p{a, b, c};
        if (!g.IsValidMove(p)) continue;
        const TileType t = g.At(a.col, a.row).type;
        bool grew = true;
        while (grew) {
            grew = false;
            std::vector<Game::Cell> cand;
            int cs = ScoreMove(g, p);
            for (int dc = -1; dc <= 1; ++dc) for (int dr = -1; dr <= 1; ++dr) {
                if (!dc && !dr) continue;
                Game::Cell e{p.back().col + dc, p.back().row + dr};
                if (e.col < 0 || e.col > 5 || e.row < 0 || e.row > 5) continue;
                if (g.At(e.col, e.row).type != t) continue;
                bool dup = false;
                for (auto &q : p) if (q.col == e.col && q.row == e.row) dup = true;
                if (dup) continue;
                auto trial = p;
                trial.push_back(e);
                if (g.IsValidMove(trial) && ScoreMove(g, trial) > cs) { cs = ScoreMove(g, trial); cand = trial; }
            }
            if (!cand.empty()) { p = cand; grew = true; }
        }
        int sc = ScoreMove(g, p);
        if (sc > bestScore) { bestScore = sc; best = p; }
    }
    return best;
}

// V26 skill-use policy — HARNESS code (a move policy like FindMove/ScoreMove above, NOT game
// logic): with drcli --skills, activate the LOWEST-numbered slot holding a fully-charged active
// skill before each move. Passive/stat-boost skills (type >= STAT_SKILL_BASE, skill.h) and
// single-use/passive cooldowns (maxCharge < 0, Skill::Deserialise @0x5ffb0) have no button use.
// Skill::CanActivate @0x60100 is deliberately NOT pre-checked here: both sides run their own check
// (rebuild Game::ActivateSkill, native Interface::ProcessInput @0x49738:86-88), so a disagreement
// shows up as a state difference instead of a silently skipped tap.
inline int ReadySkill(Game &g)
{
    for (int s = 0; s < static_cast<int>(g.skills.size()); ++s) {
        const Skill &sk = *g.skills[static_cast<std::size_t>(s)];
        if (sk.type < 0 || sk.type >= STAT_SKILL_BASE) continue;
        if (sk.maxCharge < 0 || sk.charge < sk.maxCharge) continue;
        return s;
    }
    return -1;
}

} // namespace drtest
