// One board cell. Original: class Tile, sizeof 0x270, 6x6 inline array in Game at +0x5fcc
// (cell(col,row) = Game + 0x5fcc + col*0x270 + row*0xea0). Offsets in comments are Tile+offset
// (analysis/FIELDS.md, analysis/board/*.md). Rendering-only members (quads, labels, UVs) are omitted.
#pragma once
#include <cstdint>
#include "enums.h"

namespace dr {

class Game;

struct Tile {
    Game *game = nullptr;            // +0x74
    int  col = 0, row = 0;           // +0x78 / +0x7c grid position

    TileType  type = TILE_NONE;      // +0xa0
    int       specialVariant = 3;    // +0xa4 (SetSpecialType; SetType default 3)
    EnemyType enemyType = ENEMY_NORMAL; // +0x25c, meaningful when type == TILE_SKULL
    int faceFrame = 0;                  // +0x23c skull face atlas column 0..7 (SetType RandI(0,7,0);
                                        // render: Tile::UpdateEnemyUV @0x64b04:24-31 uses it for enemyType 0)

    // enemy stats (Tile::GenerateStats, analysis/combat/ENEMY_STATS.md)
    int attack = 0;                  // +0xa8
    int defence = 0;                 // +0xac  enemy armour: damage taken = max(0, dmg - defence)
    int health = 0;                  // +0xb0
    int maxHealth = 0;               // +0xb4
    int countdown = 0;               // +0x260 ability countdown

    // per-turn / chain state
    bool matched = false;            // +0x88 cleared this move (gravity refills it)
    bool inChain = false;            // +0x8a part of the chain being drawn
    bool abilityFired = false;       // +0x264 ability already used this turn
    bool frozen = false;             // +0x265
    bool flaming = false;            // +0x266
    bool flamePulse = false;         // +0x267 (SetFlameVisibility param_3; gates the press flame preview)
    int  flameDamage = 0;            // +0x268
    int  poisonTurns = 0;            // +0x248 poison active counter
    int  poison = 0;                 // +0x24c poison accumulator
    int  poisonAddedThisTurn = 0;    // +0x250

    // special resource variants
    bool magicSword = false;         // +0x240
    bool arrow = false;              // +0x241
    bool manaPotion = false;         // +0x242
    bool acidPotion = false;         // +0x243
    bool explosivePotion = false;    // +0x244
    bool broken = false;             // +0x26c broken shield/sword
    bool promoted = false;           // +0x26d
    bool resurrectBlocked = false;   // +0x26e

    // falling animation state that affects gameplay timing only
    bool falling = false;            // +0x94
    float fallDelay = 0.0f;          // +0x6c

    bool IsEnemy() const { return type == TILE_SKULL; }
    int  GetAttack() const;          // Tile::GetAttack @ 0x62934 (tile.cpp: poison + level-1 halving)
    void AddHealth(int amount) {                       // Tile::AddHealth @ 0x63a5c
        health += amount; if (maxHealth < health) maxHealth = health;   // K11: raises max, no cap
    }

    // implemented in src/tile.cpp (R tasks)
    void SetType(TileType t, int turn, Difficulty difficulty);
    void GenerateStats(int turn, Difficulty difficulty);   // @ 0x63098
    void ActivateAbility();                                 // @ 0x64c78
    void Promote(EnemyType to);                             // @ 0x64e3c
    int  GetPoison();                                       // @ 0x62b0c (K5)
};

// Tile::UpdateEnemyUV @ 0x64b04 — the stream-0 glow-speed draw (1 when the enemy is the active
// class's mortal enemy). V24 RNG bookkeeping; tile.cpp. Callers: Promote, DemoteSpecialEnemy
// (before the demotion), AttackSkull 0x20-strip, Lich ActivateAbility.
void UpdateEnemyUVDraw(Tile &t);

} // namespace dr
