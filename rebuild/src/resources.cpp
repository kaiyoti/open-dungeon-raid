// Resource economy: Game::AddResource (per-token collection) and Game::UpdateStats
// (recompute derived stats from equipped items + stat-boost skills).
// Source: analysis/progression/RESOURCES.md + END_OF_TURN.md; every constant re-verified
// against the decomp file cited on each function. Interface calls (bars, shadows, help
// popups, labels) are presentation — routed to Platform hooks; the original's UI value
// caches (Game+0x5a84..0x5ab8) are not rebuilt (hooks fire unconditionally).
#include "dr/game.h"
#include "dr/character_class.h"
#include "dr/item.h"
#include "dr/skill.h"
#include <algorithm>

namespace dr {
namespace {

// Token type (Tile+0x7c, passed as AddResource param_3). CONFIRMED by RESOURCES.md:23-27 (type 1
// recharges a skill, 2 = acid/"bad potion" achievement 0x25, 3 = skull token that damages you) and
// the switch in Game::AddToken @0x2a934:34-81 (help ids 3/6/2/5).
enum TokenType { TOKEN_NORMAL = 0, TOKEN_SKILL_CHARGE = 1, TOKEN_ACID = 2, TOKEN_TRAP = 3 };

// Skill::GetStat @ 0x5fef4 — static global int[4]; dumped live from the original (tools/emu):
// [1,1,5,1] at game start and unchanged after menus — the base magnitude per stat-boost skill
// (STR +1 dmg, DEX +1 armour, VIT +5 HP, LUK +1 heal; Skill::Initialise(StatType) @ 0x606a4).
// V5: the old "summed rank of owned stat skills" GUESS was wrong (broke healBonus after the
// first upgrade menu, seed 777 move 38).
int SkillStatBoost(const Game &g, int statType)
{
    (void)g;
    static const int kStatBase[4] = {1, 1, 5, 1};
    if (statType < 0 || statType >= 4) return 0;   // decomp: index >= 4 -> 0
    return kStatBase[statType];
}

// Skill::GetStatChance @ 0x5ff0c — constant 5.
int SkillStatChance() { return 5; }

// CharacterClass::GetPerkCount @ 0x35f4c / GetFlawCount @ 0x35f74 — count of entries != 0xff in
// the four slots at CC+0x10..+0x1c (gated on CC+4 != 0); rebuild-side it is the non-NONE count.
int PerkCount(const CharacterClass &c) { int n = 0; for (int p : c.perks) if (p != PERK_NONE) ++n; return n; }
int FlawCount(const CharacterClass &c) { int n = 0; for (int f : c.flaws) if (f != FLAW_NONE) ++n; return n; }

} // namespace

// Game::AddResource @ 0x32210 (RESOURCES.md). One resource token collected from a cleared
// tile: tileType = Tile+0x78, tokenType = Tile+0x7c, value = Tile+0x98.
// m = 2 only in Dungeon Sprint (challenge && mode == MODE_SPRINT), else 1.
void Game::AddResource(TileType tileType, int tokenType, int value)
{
    const int m = (challenge && mode == MODE_SPRINT) ? 2 : 1;   // +0x14335 && +0x14330 == 0
    switch (tileType) {
    case TILE_POTION: {
        if (tokenType == TOKEN_SKILL_CHARGE && value >= 0 &&
            value < static_cast<int>(skills.size())) {          // mana potion: recharge skill `value`
            fx().QueueSound(SOUND_POTION);                      // 4
            Skill &s = *skills[value];                          // skills array +0xc358
            s.charge = std::min(s.charge + 1, s.maxCharge);     // +0x90 += 1, cap +0x8c
            fx().OnSkillsChanged();
            return;
        }
        if (tokenType == TOKEN_ACID) {                          // bad/acid potion
            fx().QueueSound(SOUND_DAMAGE);                      // 0x11
            hp -= healBase + healBonus;                         // +0xc1c8 + +0xc1cc
            if (hp < 0) {                                       // AddResource @0x32210:29-31
                hp = 0;
                fx().QueueAchievement(ACH_DISINTEGRATED, 100.0f);   // 0x25 @ 0x42c80000
            }
            fx().OnHealthChanged(hp);
            CheckEndGame();
            return;
        }
        fx().QueueSound(SOUND_POTION);                          // 4
        int healed = (healBase + healBonus) * (doublePotions ? 2 : 1) + hp;  // +0xc236 double flag
        hp = healed < MaxHp() ? healed : MaxHp();               // cap +0xc15c + +0xc160 @0x32210:61-63
        fx().OnHealthChanged(hp);
        if (ActiveClass().MortalEnemy() != ENEMY_POISONOUS ||   // class+0x8 mortal enemy == 9
            GetEnemyCount(ENEMY_POISONOUS) == 0)
            poisonLevel = 0;                                    // +0x14d88 cure poison
        break;
    }
    case TILE_SKULL: {
        if (tokenType == TOKEN_TRAP) {                          // explosive/decoy potion
            fx().QueueSound(SOUND_DAMAGE);                      // 0x11
            hp = std::max(0, hp - value);
            fx().OnHealthChanged(hp);
            fx().AddLabel(2, nullptr, value);                   // AddResource @0x32210:83 passes literal 2
            if (reviveAvailable && !reviveUsed) {               // +0x15cee / +0x15cec one-time revive
                reviveUsed = true;
                CheckEndGame();
                reviveUsed = false;
                return;
            }
            CheckEndGame();
            return;
        }
        fx().QueueSound(SOUND_XP);                              // 9
        fx().ShowHelp(0xd, nullptr);                                     // Interface::ShowHelpXP @ 0x489e4
        xp += m * 2;                                            // +0xc170 bar
        xpFrac += static_cast<float>(m) * xpRate;               // +0xc190 += m * +0xc194
        if (xpFrac >= 1.0f) { int w = static_cast<int>(xpFrac); xp += w; xpFrac -= static_cast<float>(w); }
        if (xp > 99) {
            xpHundreds += 100;                                  // +0xc210
            level += 1;                                         // +0xc20c
            OpenMenu(MENU_LEVEL_UP);                            // ShowMenu(0) + offers (menus.cpp)
            xp %= 100;
            if (Race() == RACE_FAUN) Healing();                 // race 4: heal to 25% max (ACH 0x32 in Healing)
            switch (level) {
            case 5:
                fx().QueueAchievement(ACH_CURIOUS, 100.0f);     // 0x1d
                if (difficulty == DIFF_HARDER && skills.empty())
                    fx().QueueAchievement(ACH_PIECE_OF_CAKE, 100.0f);   // 0x47
                break;
            case 10:
                fx().QueueAchievement(ACH_SKILLED, 100.0f);     // 0x1e
                if (difficulty == DIFF_HARDER && PerkCount(ActiveClass()) == 1 &&
                    FlawCount(ActiveClass()) == 4)
                    fx().QueueAchievement(ACH_TRAGIC_HERO, 100.0f);     // 0x46
                break;
            case 25:  fx().QueueAchievement(ACH_HEROIC, 100.0f);     break;  // 0x1f
            case 50:  fx().QueueAchievement(ACH_LEGENDARY, 100.0f);  break;  // 0x20
            case 100: fx().QueueAchievement(ACH_ASCENDANT, 100.0f);  break;  // 0x21
            default: break;
            }
        }
        break;
    }
    case TILE_COIN: {
        fx().QueueSound(SOUND_COIN);                            // 2
        fx().ShowHelp(7, nullptr);                                       // Interface::ShowHelpGold @ 0x48ae0
        const int k = doubleCoins ? 2 : 1;                      // +0xc237
        gold += k * m * 2;                                      // +0xc178 bar
        goldFrac += static_cast<float>(m) * goldRate * static_cast<float>(k);  // +0xc198 += m*+0xc19c*k
        if (goldFrac >= 1.0f) { int w = static_cast<int>(goldFrac); gold += w; goldFrac -= static_cast<float>(w); }
        if (gold > 99) {
            goldHundreds += 100;                                // +0xc214
            OpenMenu(MENU_SHOP);                                // ShowMenu(1) + offers (menus.cpp)
            gold %= 100;
        }
        break;
    }
    case TILE_SHIELD:
        if (armour == maxArmour || GetEnemyCount(ENEMY_CORROSIVE) != 0) {  // full armour, or enemy 12
            fx().QueueSound(SOUND_UP);                          // 8
            fx().ShowHelp(0xc, nullptr);                                 // Interface::ShowHelpUP @ 0x48a14
            const int k = doubleShields ? 2 : 1;                // +0xc235
            upgradePoints += k * m * 2;                         // +0xc174 bar
            upFrac += static_cast<float>(m) * upRate * static_cast<float>(k);  // +0xc1a0 += m*+0xc1a4*k
            if (upFrac >= 1.0f) { int w = static_cast<int>(upFrac); upgradePoints += w; upFrac -= static_cast<float>(w); }
            if (upgradePoints > 99) {
                upHundreds += 100;                              // +0xc218
                OpenMenu(MENU_UPGRADE);                         // ShowMenu(2) + offers (menus.cpp)
                upgradePoints %= 100;
            }
        } else {
            fx().QueueSound(SOUND_SHIELD);                      // 6
            fx().ShowHelp(0xb, nullptr);                                 // Interface::ShowHelpArmour @ 0x48a78
            armour += (armourBase + armourBonus) * (doubleShields ? 2 : 1);  // +0xc1d0 + +0xc1d4
            if (armour > maxArmour) armour = maxArmour;         // cap +0xc168
            fx().OnArmourChanged(armour);
        }
        break;
    default:
        break;                                                  // swords/specials never call AddResource
    }
}

// Game::UpdateStats @ 0x26c94 (END_OF_TURN.md). Recomputes every derived stat from scratch:
// resets to class defaults, sums the equipped items' stat slots (Item+0x94 + i*4), then
// applies the stat-boost skills. Item float stats (xp/gold/up rate) are int * 0.25f
// (ItemUpgrade::GetFloatValue @ 0x5179c, DAT_000517b4 = 0.25f); rates cap at 2.0f
// (DAT_00026e54). Crit caps at 95, armour break floors at 5.
void Game::UpdateStats()
{
    const int oldMaxArmour = maxArmour;     // +0xc168
    const int oldArmour    = armour;        // +0xc164
    const bool hadWeapon   = hasWeapon;     // +0xc224
    const int  oldBonusHp  = bonusMaxHp;    // +0xc160

    swordDamage = 0;                 // +0xc17c
    maxArmour          = 0;                 // +0xc168
    armourBreakChance  = 50;                // +0xc188 = 0x32
    critChance         = HaveFlaw(FLAW_CAUTIOUS) ? 30 : 50;   // +0xc18c, flaw 5 -> 0x1e
    poisonDamage       = 0;                 // +0xc21c
    upgradeGateLevel   = 0;                 // +0xc220
    spikes             = 0;                 // +0xc228
    blunting           = 0;                 // +0xc22c
    regen              = 0;                 // +0xc230
    hasWeapon          = false;             // +0xc224
    boostStrCount = boostDexCount = boostVitCount = boostLukCount = 0;  // +0xc1ac/0xc1b4/0xc1bc/0xc1c4
    xpRate = goldRate = upRate = 0.0f;      // +0xc194/+0xc19c/+0xc1a4 (DAT_00026e50 = 0.0f)
    bonusMaxHp         = 0;                 // +0xc160
    attackBonus        = 0;                 // +0xc184
    armourBonus        = 0;                 // +0xc1d4
    const bool notPretzel = !(challenge && mode == MODE_PRETZEL);   // +0x14335 && +0x14330 == 1

    for (const auto &sp : items) {
        const Item &it = *sp;
        swordDamage += it.stats[0];   // +0x94 sword value
        maxArmour          += it.stats[11];  // +0xc0
        critChance         += it.stats[4];   // +0xa4
        armourBreakChance  -= it.stats[14];  // +0xcc
        poisonDamage       += it.stats[3];   // +0xa0
        upgradeGateLevel   += it.stats[2];   // +0x9c
        spikes             += it.stats[12];  // +0xc4
        blunting           += it.stats[15];  // +0xd0
        regen              += it.stats[17];  // +0xd8
        if (it.stats[5] > 0) hasWeapon = true;   // +0xa8
        boostStrCount      += it.stats[8];   // +0xb4
        boostDexCount      += it.stats[18];  // +0xdc
        boostVitCount      += it.stats[19];  // +0xe0
        boostLukCount      += it.stats[9];   // +0xb8
        if (notPretzel) bonusMaxHp += it.stats[10];   // +0xbc (no HP items in Pretzel Hero)
        attackBonus        += it.stats[1];   // +0x98
        armourBonus        += it.stats[16];  // +0xd4
        xpRate  += 0.25f * static_cast<float>(it.stats[6]);   // +0xac
        goldRate += 0.25f * static_cast<float>(it.stats[7]);  // +0xb0
        upRate  += 0.25f * static_cast<float>(it.stats[13]);  // +0xc8
    }
    if (!items.empty() && xpRate > 2.0f) xpRate = 2.0f;   // clamp inside the items block (decomp)
    if (goldRate > 2.0f) goldRate = 2.0f;
    if (upRate > 2.0f) upRate = 2.0f;
    if (critChance > 95) critChance = 95;         // 0x5f
    if (armourBreakChance < 5) armourBreakChance = 5;

    // stat-boost skills: count * Skill::GetStat(type); GetStatChance() = 5 per count.
    if (notPretzel) bonusMaxHp += boostVitCount * SkillStatBoost(*this, STAT_VIT);   // GetStat(2)
    attackBonus += boostStrCount * SkillStatBoost(*this, STAT_STR);                  // GetStat(0)
    healBonus    = boostLukCount * SkillStatBoost(*this, STAT_LUK);                  // GetStat(3), assign
    armourBonus += boostDexCount * SkillStatBoost(*this, STAT_DEX);                  // GetStat(1)
    boostLukChance = boostLukCount * SkillStatChance();   // +0xc1dc
    boostVitChance = boostVitCount * SkillStatChance();   // +0xc1e4
    boostStrChance = boostStrCount * SkillStatChance();   // +0xc1ec
    boostDexChance = boostDexCount * SkillStatChance();   // +0xc1f4

    // max-HP change adjusts current HP by the same delta (gain) / clamps (loss).
    if (oldBonusHp != bonusMaxHp) {
        const int newMax = bonusMaxHp + baseMaxHp;
        if (oldBonusHp < bonusMaxHp) hp += newMax - (oldBonusHp + baseMaxHp);
        else if (newMax < hp) hp = newMax;
    }

    // armour keeps its deficit against the new max, floored at 0.
    armour = std::max(0, maxArmour - (oldMaxArmour - oldArmour));

    fx().OnArmourChanged(armour);   // covers UpdateSwordValue/UpdateSkullValue/UpdateShieldValue (UI)

    // weapon gained/lost: every active skill's max charge shifts by -1/+1 (UpdateStats @ 0x26c94
    // tail: new 0xc224==0 && old!=0 -> +1; new!=0 && old==0 -> -1; SetSkill @ 0x2be04 agrees),
    // charge follows down.
    if (hasWeapon != hadWeapon) {
        const int delta = hasWeapon ? -1 : 1;
        for (auto &skp : skills) {
            Skill &s = *skp;
            const int newMax = s.maxCharge + delta;
            s.charge = std::min(s.charge, newMax);
            s.maxCharge = newMax;
        }
        fx().OnSkillsChanged();
    }
}

// FoldoutMenu::Initialise @ 0x42794 shop-skill candidate gate (decomp line ~356:
// `(iVar36 != 0 || pGVar7[(iVar33 + 0x30d6) * 4] == 0) && IsSkillUnlocked && CanActivate`):
// the word test reads Game + 0xc358 + id*4 for id 0..0x29. That range is: the 4 skill slots
// (+0xc358..+0xc364; non-null from game start — InitNewGame @ 0x2d7a4 never writes them, measured
// 4 identical non-null Skill* at count 0, seeds 4/10 via tools/emu/v20_probe.py), the skill count
// (+0xc368), the per-type chain match counters (+0xc36c..+0xc37c, MatchTile @ 0x2ac40:48), the
// firstBoard/Activate bytes (+0xc380..+0xc384), then the SimpleLabel object at +0xc388
// (Game ctor @ 0x28c8c:86; active byte +0xc388, InitNewGame:277 zeroes it). Measured on the
// original at the first shop menu (seeds 4, 10): the label is inactive and its words hold a
// stable pattern — non-zero (0xFE21EED7) at +0xc398, +0xc3ac, +0xc3c0, +0xc3d4, +0xc3e8,
// +0xc3fc (ids 16, 21, 26, 31, 36, 41) and 0 at every other word of ids 12..41.
// UNKNOWN: if that SimpleLabel ever becomes active (regen label, AddSimpleLabel type 3), its
// words change and the gate changes; not modelled (no label state in the rebuild).
bool Game::ShopSkillGate(int id) const
{
    const int off = id * 4;                       // word offset from +0xc358
    if (off <= 0xc) return false;                 // slots: non-null from start -> gate fails
    if (off <= 0x10) return skills.empty();       // +0xc368 skill count
    if (off <= 0x24) return clearedThisChain[(off - 0x14) / 4] == 0;   // +0xc36c..+0xc37c
    if (off <= 0x28)                              // +0xc380 word: bytes c380..c383
        return !firstBoard && !firstBoardAvoidSword && !counterAttackPending && !stealTokenMode;
    if (off <= 0x2c) return carrySteal == 0;      // +0xc384 word: byte c384 (+c385..87 never written)
    if (id == 16 || id == 21 || id == 26 || id == 31 || id == 36 || id == 41)
        return false;                             // stable non-zero label words (measured)
    return true;                                  // label inactive: remaining words read 0
}

} // namespace dr
