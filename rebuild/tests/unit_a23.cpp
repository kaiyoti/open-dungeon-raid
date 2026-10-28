// A23 native unit test (test harness code only — not game logic).
// Covers the three A23 rules at the native boundary:
//  1. CheckEndGame death sets gameOver (the flag web/api.cpp dr_save guards on —
//     CheckEndGame @0x2f960:156-157 sets the native save size +0x14308 = 0, and
//     DungeonRaidSurfaceView.java:385-389 deletes the file on size 0).
//  2/3. Chain tile rules: frozen non-enemies can never join a chain, frozen ENEMIES can
//     (Tile::CanMatchType @0x645ac:15 `this[0x265]!=0 && +0xa0!=1 -> false`; ProcessTileSelection
//     @0x2c8dc:216 `+0x265==0 || +0xa0==1`), and a chain never contains a repeated tile
//     (ProcessTileSelection @0x2c8dc:163-213 truncates on backtrack; turn.cpp:126 mirrors it).
#include "dr/game.h"
#include <cstdio>
#include <vector>

using namespace dr;

static int fails = 0;
static void check(bool cond, const char *what)
{
    if (!cond) { std::printf("FAIL: %s\n", what); ++fails; }
    else       { std::printf("ok:   %s\n", what); }
}

int main()
{
    Game g;
    g.seedOverride = 12345;
    g.NewGame(DIFF_NORMAL);

    // ---- 3a. CanMatchType frozen guard @0x645ac:15 (empty chain: guard is the only gate)
    Tile sk, co, sw;
    sk.type = TILE_SKULL; sk.specialVariant = 3; sk.frozen = true;
    co.type = TILE_COIN;  co.specialVariant = 3; co.frozen = true;
    sw.type = TILE_SWORD; sw.specialVariant = 3; sw.frozen = true;
    check(g.CanMatchType(sk), "frozen ENEMY may match (CanMatchType @0x645ac:15 lets +0xa0==1 through)");
    check(!g.CanMatchType(co), "frozen coin may NOT match");
    check(!g.CanMatchType(sw), "frozen sword may NOT match");

    // ---- 3b. frozen enemy inside a chain (IsValidPrefix runs CanMatchType per tile)
    for (int i = 0; i < 3; i++) {
        Tile &t = g.board[i][0];
        t.type = TILE_SKULL; t.specialVariant = 3; t.frozen = false;
        t.arrow = false; t.magicSword = false; t.broken = false;
    }
    std::vector<Game::Cell> path = {{0, 0}, {1, 0}, {2, 0}};
    check(g.IsValidPrefix(path), "plain skull chain legal");
    g.board[1][0].frozen = true;
    check(g.IsValidPrefix(path), "chain with a FROZEN ENEMY legal (ProcessTileSelection:216 +0xa0==1)");
    g.board[1][0].frozen = false;
    g.board[1][0].type = TILE_COIN;
    check(!g.IsValidPrefix(path), "mixed skull/coin chain illegal (type rule)");
    g.board[1][0].type = TILE_SKULL;

    // ---- 2. a chain never repeats a tile (the web layer truncates; IsValidPrefix mirrors it)
    std::vector<Game::Cell> rep = {{0, 0}, {1, 0}, {0, 0}};
    check(!g.IsValidPrefix(rep), "repeated tile rejected (turn.cpp:126; original truncates @0x2c8dc:163-213)");

    // ---- 1. death sets gameOver — the guard dr_save (web/api.cpp) reports size 0 on
    g.hp = 0; g.reviveUsed = false;
    check(g.CheckEndGame(), "CheckEndGame true at hp 0 (no Last Stand)");
    check(g.gameOver, "gameOver set (dr_save kills the save on this flag; CheckEndGame @0x2f960:156-157)");

    std::printf(fails ? "A23 UNIT: %d FAILURES\n" : "A23 UNIT: all ok\n", fails);
    return fails ? 1 : 0;
}
