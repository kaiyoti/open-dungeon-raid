// Reward menus: offer generation (FoldoutMenu::Initialise @ 0x42794) and the confirm/apply path
// (FoldoutMenu::Process @ 0x41fc4, reached from ProcessInput @ 0x41d54). Menus live in the GAME
// (not the Platform): OpenMenu generates the offers with the exact RNG draws/order (MENUS.md),
// ChooseMenu applies the choice exactly like the original's confirm path. UI geometry, labels,
// tooltips and the fold animation are presentation — only fx().ShowMenu is kept for front ends.
// Offers are transient: the original does not save them (analysis/state/SAVE_FORMAT.md).
#include "dr/game.h"
#include "dr/character_class.h"
#include "dr/item.h"
#include "dr/skill.h"
#include "dr/rng.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace dr {
namespace {

// Skill::GetBaseCooldown @ 0x5ff40 — g_skillCooldowns (.data 0x778c0, 42 u32; dumped from the
// .so). Skill 33 (Wish) = -1 (single use).
const int kBaseCooldown[42] = {
    13, 16, 15, 16, 18, 18, 15, 20, 18, 18, 35, 12, 18, 15, 25, 15, 14, 15, 15, 15,
    18, 20, 18, 15, 25, 14, 12, 16, 14, 20, 24, 15, 15, -1, 14, 12, 25, 35, 18, 16, 14, 14};

// Skill::GetStat @ 0x5fef4 — static global int[4] (dumped live from the original: [1,1,5,1] at
// game start and after every menu; a constant base magnitude per stat: STR +1 dmg, DEX +1 armour,
// VIT +5 HP, LUK +1 heal).
int GetStatBoost(int statType)
{
    static const int kStatBase[4] = {1, 1, 5, 1};
    if (statType < 0 || statType >= 4) return 0;   // decomp: index >= 4 -> 0
    return kStatBase[statType];
}

// Skill::SetStatChance @ 0x5ff50 — the chance gain is GetStatChance() = 5 (@ 0x5ff0c) while the
// current chance is < 100, else 0.
int StatChanceGain(int current) { return current < 100 ? 5 : 0; }

// Skill::Initialise(SkillType, Skill*) @ 0x60984 — new skill (old = null) or rank-up of the
// owned skill `old`. Sprint (challenge && mode == SPRINT) cuts 4 off the cooldown and forces
// rank 5.
void InitSkillOffer(Skill &s, Game &g, int id, const Skill *old)
{
    const bool sprint = g.challenge && g.mode == MODE_SPRINT;   // +0x14335 && +0x14330 == 0
    s.game = &g;
    s.active = false;
    s.type = id;                                                // +0x84
    s.maxCharge = old ? (old->rank == 10 ? old->maxCharge : old->maxCharge - 1)
                      : kBaseCooldown[id];                      // +0x8c
    if (sprint) s.maxCharge -= 4;
    s.charge = old ? std::min(old->charge, s.maxCharge) : s.maxCharge;   // +0x90
    s.rank = old ? old->rank + 1 : 1;                           // +0x88
    if (sprint) s.rank = 5;
    s.rank = std::min(s.rank, 10);
}

// Skill::Initialise(StatType) @ 0x606a4 — stat-boost offer (type = 42 + StatType).
void InitStatSkill(Skill &s, Game &g, int stat)
{
    s.game = &g;
    s.active = false;
    s.type = STAT_SKILL_BASE + stat;                            // +0x84 = param + 0x2a
    s.rank = 0;
    s.maxCharge = 0;
    s.charge = 0;
    s.statHp = s.statHeal = s.statArmour = s.statDamage = 0;    // +0x1e4..0x1f0
    s.statLuckChance = s.statVitChance = s.statStrChance = s.statDexChance = 0;  // +0x1f4..0x200
    switch (stat) {
    case STAT_STR:   s.statDamage = GetStatBoost(0);
                     s.statStrChance  = StatChanceGain(g.bonusChance[TILE_SKULL][0]);   // +0xc1e8
                     break;
    case STAT_DEX:   s.statArmour = GetStatBoost(1);
                     s.statDexChance  = StatChanceGain(g.bonusChance[TILE_SHIELD][0]);  // +0xc1f0
                     break;
    case STAT_VIT:   s.statHp = GetStatBoost(2);
                     s.statVitChance  = StatChanceGain(g.bonusChance[TILE_POTION][0]);  // +0xc1e0
                     break;
    case STAT_LUK:   s.statHeal = GetStatBoost(3);
                     s.statLuckChance = StatChanceGain(g.bonusChance[TILE_COIN][0]);    // +0xc1d8
                     break;
    case STAT_DMG:   s.statDamage = 2;  break;
    case STAT_DEF:   s.statArmour = 2;  break;
    case STAT_HP:    s.statHp = 10;     break;
    case STAT_CHA:   s.statLuckChance = StatChanceGain(g.bonusChance[TILE_COIN][0]);
                     s.statStrChance  = StatChanceGain(g.bonusChance[TILE_SKULL][0]);
                     s.statDexChance  = StatChanceGain(g.bonusChance[TILE_SHIELD][0]);
                     s.statVitChance  = StatChanceGain(g.bonusChance[TILE_POTION][0]);
                     break;
    default: break;
    }
}

} // namespace

// Game::IsSkillUnlocked @ 0x248e8 — ids 0..0x1e always; ids 0x1f..0x29 (class-unique skills)
// unlock when the class whose FIRST base skill is that id reaches level 10.
bool Game::IsSkillUnlocked(int id) const
{
    if (id < 0x1f) return true;
    for (const auto &c : classes)
        if (c->baseSkills[0] == id) return c->level == 10;
    return false;
}

// CharacterClass::GetSkillPool @ 0x3707c — class 0: every unlocked id 0..0x29; classes 1..9:
// the 20 base skills with replacementSkills applied (0xff = keep base).
std::set<int> Game::GetClassSkillPool() const
{
    const CharacterClass &c = *classes[activeClass];   // ActiveClass() is non-const
    std::set<int> pool;
    if (c.klass == CLASS_ADVENTURER) {
        for (int id = 0; id < 0x2a; ++id)
            if (IsSkillUnlocked(id)) pool.insert(id);
    } else {
        for (int i = 0; i < 20; ++i)
            pool.insert(c.replacementSkills[i] == 0xff ? c.baseSkills[i] : c.replacementSkills[i]);
    }
    return pool;
}

// ---------------------------------------------------------------------------
// Game::OpenMenu — FoldoutMenu::Initialise(MenuType) @ 0x42794. Called where the original's
// Interface::ShowMenu runs (Game::AddResource @ 0x32210, Game::Upgrade @ 0x251d4). The fx() hook
// is kept for front ends; the offers and their RNG are game state.
// ---------------------------------------------------------------------------
void Game::OpenMenu(MenuType m)
{
    fx().ShowMenu(m);                       // presentation (Interface::ShowMenu @ 0x48f30)
    // S3 step 3: the menu-open jingle. Interface::ShowMenu @ 0x48f30 calls FoldoutMenu::Initialise
    // @ 0x42794, which queues it per menu type: SHOP(11) @:223 (param_2==1), LEVELUP(12) @:422
    // (param_2==0), UPGRADE(13) @:941 (param_2==2). MenuType enum (platform.h:13) matches param_2.
    if (m == MENU_SHOP)          fx().QueueSound(SOUND_SHOP);      // 11
    else if (m == MENU_LEVEL_UP) fx().QueueSound(SOUND_LEVELUP);   // 12
    else if (m == MENU_UPGRADE)  fx().QueueSound(SOUND_UPGRADE);   // 13
    // One global FoldoutMenu (Interface+0xdf0): Interface::ShowMenu @ 0x48f30 calls
    // FoldoutMenu::Initialise @ 0x42794, which sets the open flag (this[8]=1), resets the
    // selections (+0x30/+0x34 = -1, @ 0x427b2) and regenerates the offers in place. A ShowMenu
    // while a menu is open replaces the open menu — the old offers are discarded, never applied.
    menuType = static_cast<int>(m);
    menuPending = true;
    if (menuApplying) menuReopened = true;   // ShowMenu while a menu is being applied REPLACES it
                                             // (Interface::ShowMenu @ 0x48f30 -> Initialise @ 0x42794)

    if (m == MENU_SHOP) {                   // gold, RNG stream 2 (RAND_ITEM)
        std::set<int> types{0, 1, 2, 3, 4};
        for (int i = 0; i < 3; ++i) {       // 3 offers, distinct equipment slots
            int k = RandI(0, static_cast<int>(types.size()) - 1, RAND_ITEM);
            auto it = types.begin();
            std::advance(it, k);
            ItemType t = static_cast<ItemType>(*it);
            types.erase(it);
            menuShopItems[i] = std::make_unique<Item>();
            menuShopItems[i]->game = this;                      // Item+0x7c
            menuShopItems[i]->Initialise(t, baseItems[t].get()); // base = GetItem @ 0x24610 template (V8)
        }
        menuShopSkill.reset();
        menuShopSkillSlot = -1;
        const float p = (Race() == RACE_SAUREN) ? 0.5f : 0.25f;   // 0.5 @ 0x43130, 0.25 @ 0x4312c
        if (Rand(0.0f, 1.0f, RAND_ITEM) < p) {
            const int slot = RandI(0, 2, RAND_ITEM);             // which offer row shows the skill
            const bool allowOwned = Rand(0.0f, 1.0f, RAND_ITEM) < 0.1f;   // 0.1 @ 0x43144
            std::vector<int> cand;
            for (int id = 0; id <= 0x29; ++id) {
                if (id == 0x21) continue;      // native never runs the body for Wish: the
                                                // increment is do{ id++; n++; } while (id == 0x21)
                if (id == 0x18 && challenge && mode == MODE_PRETZEL) continue;
                if (!allowOwned && !ShopSkillGate(id)) continue; // word test at Game+0xc358+id*4
                // FoldoutMenu::Initialise @ 0x42794: pGVar7[(iVar33+0x30d6)*4] == 0 (the slots are
                // pre-allocated non-null, so ids 0..3 always fail — the "owned" name was wrong).
                if (!IsSkillUnlocked(id)) continue;
                Skill probe; probe.game = this; probe.type = id; // Skill::CanActivate(int) @ 0x60100
                if (!probe.CanActivate()) continue;
                cand.push_back(id);
            }
            const int pick = RandI(0, static_cast<int>(cand.size()) - 1, RAND_ITEM);
            if (pick >= 0 && pick < static_cast<int>(cand.size())) {   // native: slot is stored at
                menuShopSkillSlot = slot;                            // +0x110 BEFORE the candidate
                // loop (Initialise @0x42794, `*(this+0x110)=RandI(0,2)`), so this is never OOB
                // (slot = 0..2). Empty cand is unreachable: >=6 ids are always unlocked, pass the
                // word gate and take CanActivate's `default: true` (@0x60100) — the native would
                // read RandI(0,-1)+cand[0] of a null vector there (its UB, not reproduced).
                menuShopSkill = std::make_unique<Skill>();
                InitSkillOffer(*menuShopSkill, *this, cand[pick], nullptr);
            }
        }
        return;
    }

    if (m == MENU_UPGRADE) {                // upgrade points, RNG stream 4 (RAND_UPGRADE)
        std::set<int> types{0, 1, 2, 3, 4};
        for (int i = 0; i < 4; ++i) {       // 4 offers, distinct equipment slots
            int k = RandI(0, static_cast<int>(types.size()) - 1, RAND_UPGRADE);
            auto it = types.begin();
            std::advance(it, k);
            ItemType t = static_cast<ItemType>(*it);
            types.erase(it);
            menuUpTypes[i] = t;
            menuUpgrades[i] = RollItemUpgrade(*items[t]);        // ItemUpgrade::Initialise @ 0x523c8
        }
        return;
    }

    // MENU_LEVEL_UP — XP, RNG stream 3 (RAND_SKILL). 4 rows: skill rows first, stat rows fill up.
    const bool sprint = challenge && mode == MODE_SPRINT;       // decomp iVar33 (1 = sprint)
    const bool pretzel = challenge && mode == MODE_PRETZEL;
    const int count = static_cast<int>(skills.size());          // +0xc368
    std::set<int> pool = (count == 4) ? std::set<int>{} : GetClassSkillPool();
    if (count == 4)
        for (const auto &s : skills) pool.insert(s->type);
    bool allMaxed = true;                                       // decomp bVar1
    if (count >= 1) {
        for (const auto &s : skills) {
            if (sprint) { pool.erase(s->type); continue; }      // sprint: owned leave the pool
            if (s->rank > 9) { pool.erase(s->type); continue; } // maxed: no more upgrades
            if (s->type == 0x21) { pool.erase(0x21); continue; }
            allMaxed = false;
        }
    }
    if (sprint) pool.erase(0x16);                               // Trap excluded in sprint

    std::set<int> stats;
    for (int st = 0; st < 8; ++st)
        if (st != 7 ||
            bonusChance[TILE_SKULL][0] + boostStrChance  < 100 ||   // +0xc1e8 + +0xc1ec
            bonusChance[TILE_COIN][0]  + boostLukChance  < 100 ||   // +0xc1d8 + +0xc1dc
            bonusChance[TILE_POTION][0] + boostVitChance < 100 ||   // +0xc1e0 + +0xc1e4
            bonusChance[TILE_SHIELD][0] + boostDexChance < 100)     // +0xc1f0 + +0xc1f4
            stats.insert(st);
    if (pretzel) { pool.erase(0x18); pool.erase(7); stats.erase(6); }

    int rows = (count == 3 && allMaxed) ? (pool.empty() ? 0 : 1)
                                        : std::min(static_cast<int>(pool.size()), 2);
    menuSkillRows = rows;
    for (int i = 0; i < 4; ++i) menuLevelSkills[i].reset();

    for (int i = 0; i < rows; ++i) {
        int k = RandI(0, static_cast<int>(pool.size()) - 1, RAND_SKILL);
        auto it = pool.begin();
        std::advance(it, k);
        int id = *it;
        Skill *old = nullptr;
        for (auto &s : skills)
            if (s->type == id) { old = s.get(); break; }
        if (i == 0 && level == 2 && ActiveClass().klass != CLASS_ADVENTURER) {
            const int cid = ActiveClass().baseSkills[0];        // 2nd level grants the class skill
            if (!(sprint && cid == 0x21)) {
                id = cid;
                old = nullptr;
                for (auto &s : skills)
                    if (s->type == id) { old = s.get(); break; }
            }
        }
        if (i == 0 && count == 3 && old == nullptr) {
            // Initialise @0x42794, skill-row loop LAB_000432fa: `row==0 && count==3 && old==0`
            // -> _M_erase(pool), then (only when NOT sprint) re-insert every owned skill with
            // rank<10 && type!=0x21. So it forces row 1 (rows==2) to be a rank-up of a skill you
            // already own — MENUS.md's "guarantees a new-skill slot" guess had it backwards; with
            // rows==1 (all maxed) the clear changes no draw. Sprint leaves the pool empty here
            // (the native would draw RandI(0,-1) from it — its UB, not reproduced).
            pool.clear();
            if (!sprint)
                for (const auto &s : skills)
                    if (s->rank < 10 && s->type != 0x21) pool.insert(s->type);
        } else {
            pool.erase(id);
        }
        menuLevelSkills[i] = std::make_unique<Skill>();
        InitSkillOffer(*menuLevelSkills[i], *this, id, old);
    }
    for (int i = rows; i < 4; ++i) {
        int k = RandI(0, static_cast<int>(stats.size()) - 1, RAND_SKILL);
        auto it = stats.begin();
        std::advance(it, k);
        int st = *it;
        stats.erase(it);
        menuLevelSkills[i] = std::make_unique<Skill>();
        InitStatSkill(*menuLevelSkills[i], *this, st);
    }
}

// ---------------------------------------------------------------------------
// Game::ChooseMenu — apply the confirmed choice exactly as FoldoutMenu::Process @ 0x41fc4 does
// at the end of the close animation: menu 1 -> Game::SetItem + UpdateStats (+ Skill::Activate of
// the offered skill when its row was picked); menu 2 -> Item::AddUpgrade + UpdateStats;
// menu 0 -> Game::SetSkill twice (the two picked rows). choice/choice2 are 0-based row indices
// (the original's button ids 0xe..0x11 minus 0xe).
// ---------------------------------------------------------------------------
void Game::ChooseMenu(int choice, int choice2)
{
    if (!menuPending) return;
    const int type = menuType;

    // S4 step 3: the confirm-release sound (the "2nd site" of SOUND_DIFF). FoldoutMenu::ProcessInput
    // @ 0x41d54:129-146 — touch release (Game+0x20 == 2) inside quad 7 with fm+0x21 set: QueueSound
    // by menu type fm+0xc: 1 -> 0xb SHOP (ELF 0x31e80), 2 -> 0xd UPGRADE (0x31e76), 0 -> 0xc
    // LEVELUP (0x31e76 block, id 0xc). This is the 2nd queue per menu (1st = FoldoutMenu::Initialise
    // @ 0x42794, wired in OpenMenu above) and runs before FoldoutMenu::Process @ 0x41fc4 applies
    // the choice — mirrored here at the head of the apply.
    if (type == MENU_SHOP)          fx().QueueSound(SOUND_SHOP);      // 11
    else if (type == MENU_UPGRADE)  fx().QueueSound(SOUND_UPGRADE);   // 13
    else if (type == MENU_LEVEL_UP) fx().QueueSound(SOUND_LEVELUP);   // 12

    menuApplying = true;
    if (type == MENU_SHOP && choice >= 0 && choice < 3) {
        Item *offer = menuShopItems[choice].get();
        items[offer->type] = std::move(menuShopItems[choice]);  // Game::SetItem @ 0x24644
        UpdateStats();                                          // @ 0x26c94
        if (menuShopSkillSlot >= 0 && menuShopSkillSlot == choice) {
            menuShopSkill->Activate();                          // Skill::Activate @ 0x60210
            if (Race() == RACE_SAUREN)
                fx().QueueAchievement(ACH_STAY_A_WHILE, 1.0f);  // 0x31 @ 0x40000000 (Process)
        }
        menuShopItems[choice].reset();
        menuShopSkill.reset();
        menuShopSkillSlot = -1;
    } else if (type == MENU_UPGRADE && choice >= 0 && choice < 4) {
        items[menuUpTypes[choice]]->AddUpgrade(menuUpgrades[choice]);   // Item::AddUpgrade @ 0x4dc08
        UpdateStats();
    } else if (type == MENU_LEVEL_UP) {
        for (int pick : {choice, choice2}) {
            if (pick < 0 || pick > 3 || !menuLevelSkills[pick]) continue;
            std::unique_ptr<Skill> s = std::move(menuLevelSkills[pick]);
            if (s->type < STAT_SKILL_BASE) {                    // active skill: Game::SetSkill @ 0x2be04
                int idx = -1;
                for (int i = 0; i < static_cast<int>(skills.size()); ++i)
                    if (skills[i]->type == s->type) { idx = i; break; }
                if (idx >= 0) {
                    skills[idx] = std::move(s);                 // replace (old skill discarded)
                } else {
                    skills.push_back(std::move(s));             // append, +0xc368 += 1
                    Skill &ns = *skills.back();
                    if (hasWeapon) {                            // +0xc224: weapon costs 1 charge
                        ns.maxCharge -= 1;
                        ns.charge = std::min(ns.charge, ns.maxCharge);
                    }
                    if (static_cast<int>(skills.size()) == 4 && HavePerk(PERK_CUNNING)) {   // perk 3
                        ns.maxCharge -= 1;
                        ns.charge = std::min(ns.charge, ns.maxCharge);
                    }
                }
                fx().OnSkillsChanged();                         // Interface::UpdateSkills
            } else {                                            // stat boost: SetSkill stat branch
                healBase   += s->statHeal;                      // +0xc1c8
                armourBase += s->statArmour;                    // +0xc1d0
                bonusChance[TILE_COIN][0]   = std::min(100, bonusChance[TILE_COIN][0]   + s->statLuckChance);  // +0xc1d8
                bonusChance[TILE_POTION][0] = std::min(100, bonusChance[TILE_POTION][0] + s->statVitChance);   // +0xc1e0
                bonusChance[TILE_SKULL][0]  = std::min(100, bonusChance[TILE_SKULL][0]  + s->statStrChance);   // +0xc1e8
                bonusChance[TILE_SHIELD][0] = std::min(100, bonusChance[TILE_SHIELD][0] + s->statDexChance);   // +0xc1f0
                if (!(challenge && mode == MODE_PRETZEL) && s->statHp > 0) {
                    baseMaxHp += s->statHp;                     // +0xc15c
                    hp        += s->statHp;                     // +0xc158
                }
                if (s->statDamage > 0)
                    baseDamage += s->statDamage;                // +0xc180
                switch (s->type) {                              // boost counters +0xc1a8..0xc1c0
                case STAT_SKILL_BASE + STAT_STR: unknownC1A8[0] += 1; break;
                case STAT_SKILL_BASE + STAT_DEX: unknownC1A8[1] += 1; break;
                case STAT_SKILL_BASE + STAT_VIT: unknownC1A8[2] += 1; break;
                case STAT_SKILL_BASE + STAT_LUK: unknownC1A8[3] += 1; break;
                default: break;
                }
            }
        }
    }

    menuApplying = false;
    if (menuReopened) {
        menuReopened = false;                     // the apply opened a new menu (shop skill row:
                                                  // Skill::Activate @ 0x60210 -> Game::Upgrade
                                                  // @ 0x251d4 ShowMenu(2)) — it stays open
    } else {
        menuPending = false;
        menuType = -1;
    }
    DrainTokens();                                // V23: menu closed -> frozen tokens land now
    if (attackPending) {                          // V12: deferred attack + EndOfTurn (turn.cpp)
        attackPending = false;
        FinishTurnAfterMenus();
    } else if (endOfTurnPending) {                // K5: menu opened mid-attack, resume EndOfTurn
        endOfTurnPending = false;
        EndOfTurn();
        DrainTokens();
    }
}

} // namespace dr
