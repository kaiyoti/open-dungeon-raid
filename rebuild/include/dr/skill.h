// Active or stat-boost skill. Original: class Skill (derives Upgrade); analysis/progression/SKILL_SKETCH.md,
// SKILLS.md. type < SKILL_COUNT = active skill; type = 42 + StatType = stat boost from a level-up.
#pragma once
#include "enums.h"

namespace dr {

class Game;
constexpr int STAT_SKILL_BASE = 42;

struct Skill {
    Game *game = nullptr;          // +0x7c
    bool active = false;           // +0x80
    int  type = SKILL_NONE;        // +0x84 SkillType, or STAT_SKILL_BASE + StatType
    int  rank = 0;                 // +0x88 (< 10)
    int  maxCharge = 0;            // +0x8c cooldown length
    int  charge = 0;               // +0x90 current charge; usable when charge == maxCharge
    // stat-boost skill payload (Skill::Initialise(StatType) @ 0x606a4; applied by
    // Game::SetSkill @ 0x2be04; only meaningful for type >= STAT_SKILL_BASE)
    int statHp = 0;                // +0x1e4 max HP gain (VIT 2, HP 6)
    int statHeal = 0;              // +0x1e8 heal per potion (LUK 3)
    int statArmour = 0;            // +0x1ec armour per shield (DEX 1, DEF 5)
    int statDamage = 0;            // +0x1f0 skull attack (STR 0, DMG 4)
    int statLuckChance = 0;        // +0x1f4 coin bonus-chance gain (LUK 3, CHA 7)
    int statVitChance = 0;         // +0x1f8 potion bonus-chance gain (VIT 2, CHA 7)
    int statStrChance = 0;         // +0x1fc skull bonus-chance gain (STR 0, CHA 7)
    int statDexChance = 0;         // +0x200 shield bonus-chance gain (DEX 1, CHA 7)

    bool IsReady() const { return charge >= maxCharge; }
    bool CanActivate() const;      // @ 0x60100 / 0x60204 (R task)
    void Activate();               // @ 0x60210 (R task)
};

} // namespace dr
