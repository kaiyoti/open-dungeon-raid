// Y6 K10 unit test (test harness code only — not game logic).
// Exercises the two Tile::CanMatchType @0x645ac branches that K10 left unexercised:
//  - mortal-enemy branch: a charged phylactery lich (type1 +0x25c==0x20 && +0x260!=0,
//    Game::AttackSkull @0x2f630:33-38) links to other charged phylacteries (@:61-67,73-74);
//  - coin-wildcard: coin<->charged phylactery in both directions (@:58-60,70-72).
// A mortal lich (+0x260==0) is NOT a phylactery: plain skull rules (@:47-56).
// Path B (@:77-82): after an arrow's bridged tile only a skull/sword may follow; phylactery false.
// Every expectation is a native measurement: tasks/Y6/y6_emu.json S1-S14 (tools/emu/y6_probe.py).
#include "dr/game.h"
#include <cstdio>

using namespace dr;

static int fails = 0;
static void check(bool cond, const char *what)
{
    if (!cond) { std::printf("FAIL: %s\n", what); ++fails; }
    else       { std::printf("ok:   %s\n", what); }
}

static Tile mk(TileType ty, EnemyType e = ENEMY_NORMAL, int cd = 0, bool arw = false)
{
    Tile t; t.type = ty; t.enemyType = e; t.countdown = cd; t.arrow = arw;
    t.specialVariant = 3;   // normal tile (Tile::SetType @0x64ef8:22)
    return t;
}

int main()
{
    Game g;
    g.seedOverride = 12345;
    g.NewGame(DIFF_NORMAL);

    Tile coin  = mk(TILE_COIN);
    Tile sword = mk(TILE_SWORD);
    Tile arrow = mk(TILE_SWORD, ENEMY_NORMAL, 0, true);          // +0x241 (MakeArrow @0x64294:18)
    Tile phyl  = mk(TILE_SKULL, ENEMY_LICH, 1);                  // phylactery (AttackSkull @0x2f630:33)
    Tile phyl2 = mk(TILE_SKULL, ENEMY_LICH, 1);
    Tile lich  = mk(TILE_SKULL, ENEMY_LICH, 0);                  // mortal lich: +0x260 == 0

    // ---- coin-wildcard (emu S1/S2: both directions TRUE)
    g.chain.clear(); g.chain.push_back(&coin);
    check(g.CanMatchType(phyl),  "S1 coin->phyl TRUE (coin-wildcard @:70-72)");
    g.chain.clear(); g.chain.push_back(&phyl);
    check(g.CanMatchType(coin),  "S2 phyl->coin TRUE (coin-wildcard @:58-60)");

    // ---- mortal-enemy branch (emu S3: phyl<->phyl TRUE; S12 3rd call TRUE)
    g.chain.clear(); g.chain.push_back(&phyl);
    check(g.CanMatchType(phyl2), "S3 phyl->phyl TRUE (mortal-enemy @:73-74)");

    // ---- phylactery vs sword/skull (emu S4/S5/S13: FALSE)
    g.chain.clear(); g.chain.push_back(&sword);
    check(!g.CanMatchType(phyl), "S4 sword->phyl FALSE (@:70-75 fall-through)");
    g.chain.clear(); g.chain.push_back(&phyl);
    check(!g.CanMatchType(sword), "S5 phyl->sword FALSE (@:58-63)");

    // ---- mortal lich = plain skull (emu S6: sword->lich TRUE; S8 3rd: coin->lich FALSE)
    g.chain.clear(); g.chain.push_back(&sword);
    check(g.CanMatchType(lich),  "S6 sword->mortal-lich TRUE (plain @:47-56)");
    g.chain.clear(); g.chain.push_back(&coin);
    check(!g.CanMatchType(lich), "S8 coin->mortal-lich FALSE (wildcard needs +0x260!=0)");

    // ---- Path B: tile after the arrow's bridged tile (emu S9/S10/S14/S8)
    g.chain.clear(); g.chain.push_back(&arrow); g.chain.push_back(&coin);
    check(g.CanMatchType(sword), "S9 arrow->coin->sword TRUE (PathB @:81)");
    g.chain.clear(); g.chain.push_back(&arrow); g.chain.push_back(&coin);
    check(g.CanMatchType(lich),  "S10 arrow->coin->skull TRUE (PathB @:81)");
    g.chain.clear(); g.chain.push_back(&arrow); g.chain.push_back(&coin);
    check(!g.CanMatchType(phyl), "S14 arrow->coin->phyl FALSE (PathB fall @:78-82,86)");
    g.chain.clear(); g.chain.push_back(&arrow); g.chain.push_back(&coin);
    check(!g.CanMatchType(coin), "PathB coin FALSE (@:81: only skull/sword)");

    // ---- arrow shortcut is fromInput-only (emu S7 true; dimming param_2=true skips @:28-29)
    g.chain.clear(); g.chain.push_back(&arrow);
    check(g.CanMatchType(coin, false),  "S7 arrow->coin TRUE (shortcut, fromInput=false)");
    g.chain.clear(); g.chain.push_back(&arrow);
    check(!g.CanMatchType(coin, true),  "dimming arrow->coin FALSE (shortcut needs param_2=false)");

    // ---- dimming parity: wildcard applies with fromInput=true (emu S1 dim: phyl bright on coin chain)
    g.chain.clear(); g.chain.push_back(&coin);
    check(g.CanMatchType(phyl, true),  "dimming coin-chain keeps phyl bright (wildcard, param_2=true)");

    std::printf(fails ? "Y6: %d FAIL\n" : "Y6: all ok (%d fails)\n", fails);
    return fails ? 1 : 0;
}
