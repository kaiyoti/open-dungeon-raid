// Board-collection skill effects: Slash, Fireball, Charge, Wish, Hack, Greed.
// Moved out of skills.cpp (R12). Sources: analysis/progression/SKILLS.md rows 11/28/30/33/36/38
// and the decomp of Game::CollectRandomRow @ 0x30c8c, CollectRandomBlock @ 0x30554,
// CollectEdgeTiles @ 0x308c8, CollectBoard @ 0x30a2c, ConvertColumnToSwords @ 0x30b88,
// KillSkullsNextToCoins @ 0x306fc, CheckShockAwe @ 0x28318. RNG stream RAND_OTHER (5) for the
// random row/col/centre (analysis/state/RNG.md). The +0xc150/+0xc154 float pair every function
// ends with (literals 0.0f / 1.0f) is the attack-sequence animation timer (FIELDS.md) — not
// modelled headless. Special enemies (skull +0x25c != 0) are never collected/converted.
#include "dr/game.h"
#include "dr/rng.h"

namespace dr {
namespace {

// MatchTile decomp args (chain.cpp convention): 5 = collect at the tile's own type,
// 7 = clear with no reward (used for acid potions, which would hurt the player).
constexpr TileType MATCH_USE_OWN   = static_cast<TileType>(5);
constexpr TileType MATCH_NO_REWARD = static_cast<TileType>(7);

// Platform::AddFlare colour kinds (chain.cpp: 0 red, 1 yellow; skills.cpp: 2..7).
// Original RGBA bytes cited; FLARE_ORANGE here (240,150,16) is the blast orange.
constexpr int FLARE_YELLOW = 1;   // 255,255,0,255   Slash / Charge
constexpr int FLARE_ORANGE = 5;   // 240,150,16,255  Fireball / Wish / Hack / Greed

} // namespace

// ---------------------------------------------------------------------------
// Game::CheckShockAwe @ 0x28318 — "Shock & Awe" achievement: Slash, Fireball, Charge and
// Wish all used in the same turn (+0x15e19..0x15e1c all set; EndOfTurn clears them).
// ---------------------------------------------------------------------------
void Game::CheckShockAwe()
{
    if (slashUsed && fireballUsed && chargeUsed && wishUsed)
        fx().QueueAchievement(ACH_SHOCK_AWE, 100.0f);        // 0x3f @ 0x42c80000
}

namespace {

// Shared tail of the collect skills: gravity + refill, then clear the chain-draw flags
// (+0x8a on all 36 tiles) unless the +0x14d94 turn marker is set.
void FinishCollect(Game &g)
{
    g.MoveMatchedTiles();                                    // @ 0x2fd7c
    if (!g.turnSkillMarker)                                  // +0x14d94 == 0
        for (int col = 0; col < BOARD_SIZE; ++col)
            for (int row = 0; row < BOARD_SIZE; ++row)
                g.board[col][row].inChain = false;           // +0x8a
    // +0xc150/+0xc154 <- 0.0f/1.0f: animation timers, not modelled
}

// Shared preamble: zero the per-chain resource counters (+0xc36c..0xc37c) and the
// blast-sound-once flag (+0x142f4).
void BeginCollect(Game &g)
{
    for (int i = 0; i < TILE_TYPE_COUNT; ++i) g.clearedThisChain[i] = 0;
    g.explosionSoundPlayed = false;
}

} // namespace

// ---------------------------------------------------------------------------
// Game::CollectRandomRow @ 0x30c8c — Slash (skill 11): collect one random row.
// Acid potions clear with no reward; special enemies are skipped. If Hack is active
// (+0x14da9) the two skills combine into CollectBoard instead. A row of 6 skulls is
// ACH_SLASHER. Flare yellow (255,255,0,255).
// ---------------------------------------------------------------------------
void Game::CollectRandomRow()
{
    slashUsed = true;                                        // +0x15e19
    CheckShockAwe();                                         // @ 0x28318
    if (!hackCombo) {                                        // +0x14da9 == 0
        slashCombo = true;                                   // +0x14daa
        int row = RandI(0, 5, RAND_OTHER);                   // +0x15e19 branch: RandI(0,5,5)
        BeginCollect(*this);
        int skulls = 0;
        for (int col = 0; col < BOARD_SIZE; ++col) {
            Tile &t = At(col, row);
            if (t.type == TILE_SKULL) {
                if (t.enemyType != ENEMY_NORMAL) continue;   // +0x25c: special enemy skipped
                MatchTile(t, MATCH_USE_OWN);                 // 5: normal skull, loot as skull
                ++skulls;
            } else if (t.type == TILE_POTION && t.acidPotion) {
                MatchTile(t, MATCH_NO_REWARD);               // 7: acid potion clears, no heal
            } else {
                MatchTile(t, MATCH_USE_OWN);                 // 5
            }
            fx().AddFlare(&t, FLARE_YELLOW);                 // AddFlare(1, cell, ff,ff,00,ff)
        }
        FinishCollect(*this);
        if (skulls == BOARD_SIZE)
            fx().QueueAchievement(ACH_SLASHER, 100.0f);      // 0x3c: whole row was skulls
    } else {                                                 // Slash + Hack combo
        hackCombo = false;                                   // +0x14da9 = 0
        slashCombo = false;                                  // +0x14daa = 0
        CollectBoard();                                      // @ 0x30a2c
    }
}

// ---------------------------------------------------------------------------
// Game::CollectRandomBlock @ 0x30554 — Fireball (skill 28): collect a random 3x3 block.
// Centre row/col each RandI(1,4) so the block always fits; acid potions clear with no
// reward; special enemies are skipped. Flare orange (240,150,16,255).
// ---------------------------------------------------------------------------
void Game::CollectRandomBlock()
{
    fireballUsed = true;                                     // +0x15e1a
    CheckShockAwe();                                         // @ 0x28318
    // V29: the FIRST RandI(1,4,5) @ 0x30554:25 is the ROW centre (iVar1 -> iVar3 = iVar1-1,
    // 0xea0 row stride, MatchTile row = iVar5); the SECOND @ :26 is the COL centre
    // (iVar2 -> iVar7 = iVar2-1, 0x270 col stride). Seed 16 m68: native draws (4,1) -> block
    // cols 0..2 rows 3..5; the old swapped order gave cols 3..5 rows 0..2 (9 tokens, 1 skull).
    int centreRow = RandI(1, 4, RAND_OTHER);                 // RandI(1,4,5) x2 — first = row
    int centreCol = RandI(1, 4, RAND_OTHER);                 // second = col
    BeginCollect(*this);
    for (int col = centreCol - 1; col <= centreCol + 1; ++col)
        for (int row = centreRow - 1; row <= centreRow + 1; ++row) {
            Tile &t = At(col, row);
            if (t.type == TILE_SKULL) {
                if (t.enemyType != ENEMY_NORMAL) continue;   // +0x25c: special enemy skipped
                MatchTile(t, MATCH_USE_OWN);                 // 5
            } else if (t.type == TILE_POTION && t.acidPotion) {
                MatchTile(t, MATCH_NO_REWARD);               // 7: acid potion, no reward
            } else {
                MatchTile(t, MATCH_USE_OWN);                 // 5
            }
            fx().AddFlare(&t, FLARE_ORANGE);                 // AddFlare(1, cell, f0,96,10,ff)
        }
    FinishCollect(*this);
}

// ---------------------------------------------------------------------------
// Game::CollectEdgeTiles @ 0x308c8 — Charge (skill 38): collect every tile on the board
// perimeter (row/col 0 or 5); interior untouched; special enemies skipped.
// Flare yellow (255,255,0,255).
// ---------------------------------------------------------------------------
void Game::CollectEdgeTiles()
{
    chargeUsed = true;                                       // +0x15e1b
    CheckShockAwe();                                         // @ 0x28318
    BeginCollect(*this);
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            if (col != 0 && col != BOARD_SIZE - 1 &&
                row != 0 && row != BOARD_SIZE - 1) continue; // interior tiles skipped
            Tile &t = At(col, row);
            if (t.type == TILE_SKULL && t.enemyType != ENEMY_NORMAL) continue;  // +0x25c
            MatchTile(t, MATCH_USE_OWN);                     // 5
            fx().AddFlare(&t, FLARE_YELLOW);                 // AddFlare(1, cell, ff,ff,00,ff)
        }
    FinishCollect(*this);
}

// ---------------------------------------------------------------------------
// Game::CollectBoard @ 0x30a2c — Wish (skill 33): collect the whole board. Special
// enemies are skipped. +0x14dab (noForcedCoin) is set for the duration of the collect.
// Flare orange (240,150,16,255).
// ---------------------------------------------------------------------------
void Game::CollectBoard()
{
    wishUsed = true;                                         // +0x15e1c
    CheckShockAwe();                                         // @ 0x28318
    noForcedCoin = true;                                     // +0x14dab (board.cpp extras)
    BeginCollect(*this);
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            Tile &t = At(col, row);
            if (t.type == TILE_SKULL && t.enemyType != ENEMY_NORMAL) continue;  // +0x25c
            MatchTile(t, MATCH_USE_OWN);                     // 5
            fx().AddFlare(&t, FLARE_ORANGE);                 // AddFlare(1, cell, f0,96,10,ff)
        }
    FinishCollect(*this);
    noForcedCoin = false;                                    // +0x14dab = 0
}

// ---------------------------------------------------------------------------
// Game::ConvertColumnToSwords @ 0x30b88 — Hack (skill 30): turn one random column into
// swords (special enemies keep their tile). If Slash is active (+0x14daa) the pair
// combines into CollectBoard. No gravity here — tiles are converted, not collected.
// Flare orange (240,150,16,255).
// ---------------------------------------------------------------------------
void Game::ConvertColumnToSwords()
{
    if (!slashCombo) {                                       // +0x14daa == 0
        hackCombo = true;                                    // +0x14da9
        int col = RandI(0, 5, RAND_OTHER);                   // RandI(0,5,5)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            Tile &t = At(col, row);
            if (t.type == TILE_SKULL && t.enemyType != ENEMY_NORMAL) continue;  // +0x25c
            t.SetType(TILE_SWORD, turn, difficulty);         // SetType(4, +0xc, +0x14328)
            fx().AddFlare(&t, FLARE_ORANGE);                 // AddFlare(1, cell, f0,96,10,ff)
        }
        // +0xc150/+0xc154 <- 0.0f/1.0f: animation timers, not modelled
    } else {                                                 // Hack + Slash combo
        hackCombo = false;                                   // +0x14da9 = 0
        slashCombo = false;                                  // +0x14daa = 0
        CollectBoard();                                      // @ 0x30a2c
    }
}

// ---------------------------------------------------------------------------
// Game::KillSkullsNextToCoins @ 0x306fc — Greed (skill 36): kill (collect) every NORMAL
// enemy that sits next to a coin: any of the 8 neighbours (rows row-1..row+1 x cols col-1..col+1;
// in the decomp uVar8 starts at -1 and advances with the row counter, so it is always row-1 —
// review fix of the earlier "skips the tile above" reading). No skill flag, no CheckShockAwe.
// Flare orange (240,150,16,255).
// ---------------------------------------------------------------------------
void Game::KillSkullsNextToCoins()
{
    BeginCollect(*this);
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            Tile &t = At(col, row);
            if (t.type != TILE_SKULL || t.enemyType != ENEMY_NORMAL) continue;  // +0xa0/+0x25c
            bool coinNear = false;
            for (int r = row - 1; r <= row + 1 && !coinNear; ++r)      // rows row-1..row+1
                for (int c = col - 1; c <= col + 1; ++c) {
                    if (r == row && c == col) continue;                // the skull itself
                    if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) continue;
                    if (At(c, r).type == TILE_COIN) { coinNear = true; break; }
                }
            if (!coinNear) continue;
            MatchTile(t, MATCH_USE_OWN);                     // 5: kill, loot as skull
            fx().AddFlare(&t, FLARE_ORANGE);                 // AddFlare(1, cell, f0,96,10,ff)
        }
    FinishCollect(*this);
}

} // namespace dr
