// Item system: Item::Initialise / GenerateStats / AddUpgrade and the ItemUpgrade stat roll
// (GetStatUpgrade / GetStatDowngrade / IsUpgradeAllowed / GetRandomStat / Initialise).
// Source: analysis/progression/ITEM_STATS.md + ITEM_SKETCH.md; every constant re-verified
// against the decomp file cited on each function. Name/label text (GenerateName, MakeStats,
// InitStats description lines) and icons are presentation — not rebuilt.
#include "dr/game.h"
#include "dr/item.h"
#include "dr/rng.h"
#include <algorithm>
#include <set>

namespace dr {
namespace {

// The GOT-relative Game flag pair (decomp "0x1d8.st_name + Game + 1" and "0x1d7.st_info + Game").
// __DT_SYMTAB base = .dynsym VMA 0x25b4 + image base 0x10000 = 0x125b4, so the two addresses are
// Game+0x14335 (challenge byte) and Game+0x14330 (active mode int). The pair is "set" exactly when
// challenge && mode == PRETZEL. Verified live (tools/emu/v4_probe.py): pretzel -> 1/1, sprint &
// normal -> actMode 0. Same pair gates all three sites: Item::GenerateStats@0x50ee4 case 4 fresh
// branch (early return, no stat10), ItemUpgrade::GetStatUpgrade@0x51888 case 10 (return 0),
// ItemUpgrade::IsUpgradeAllowed@0x517b8 case 10 (return mode!=PRETZEL).
bool SpecialFlagPairSet(const Game &g) { return g.challenge && g.mode == MODE_PRETZEL; }

// ItemUpgrade::GetStatUpgrade @ 0x51888 (ITEM_STATS.md "Upgrade amount").
// +0xc220 = sum of Item+0x9c over the owned items (UpdateStats @0x26c94:43 reset, :78-79 accumulate);
// it gates type 2 (IsUpgradeAllowed @0x517b8:14 needs < 0x19) and its amounts (this function).
// +0xc188 = armourBreakChance (UpdateStats clamps it to >= 5; case 0xe below uses 100 - it).
int GetStatUpgrade(int type, const Game &g)
{
    switch (type) {
    case 0: case 1: case 5: case 6: case 7: case 8: case 9:
    case 0xb: case 0xc: case 0xd: case 0xf: case 0x10: case 0x12: case 0x13:
        return 1;
    case 2: {                                   // scales with +0xc220
        int x = g.upgradeGateLevel;
        if (x < 0xf) return 5;                  // (x<0x17 && x<0x15) && (x<0x12 && x<0xf)
        if (x < 0x15) return 3;
        return 2;                               // original falls through to case 3
    }
    case 3:  return 2;
    case 4:  return 10;                         // break -> return 10
    case 10: return SpecialFlagPairSet(g) ? 0 : 10;   // GetStatUpgrade@0x51888 case 10
    case 0xe: {                                 // scales with +0xc188 = armourBreakChance
        if (g.armourBreakChance < 100) {
            int d = 100 - g.armourBreakChance;
            if (d > 0x57) return 2;
            if (d > 0x55) return 2;
            if (d > 0x52) return 3;
            if (d > 0x4f) return 3;
            if (d > 0x4a) return 5;
            if (d > 0x45) return 5;
        }
        return 10;
    }
    case 0x11: return g.HavePerk(PERK_FAITHFUL) ? 2 : 1;   // HavePerk(5)
    default:  return 0;
    }
}

// ItemUpgrade::GetStatDowngrade @ 0x5193c: types 2/0xe use their own tables, rest delegate.
int GetStatDowngrade(int type, const Game &g)
{
    if (type == 2) {
        int x = g.upgradeGateLevel;
        if (x < 0x12) return 5;                 // (x<0x15 && x<0x12)
        if (x < 0x17) return 3;
        return 2;
    }
    if (type == 0xe) {
        if (g.armourBreakChance < 100) {
            int d = 100 - g.armourBreakChance;
            if (d > 0x57) return 2;
            if (d > 0x55) return 3;
            if (d > 0x52) return 3;
            if (d > 0x4f) return 5;
            if (d > 0x4a) return 5;
        }
        return 10;
    }
    return GetStatUpgrade(type, g);
}

// ItemUpgrade::IsUpgradeAllowed @ 0x517b8. Float threshold DAT_00051884 = 2.0f
// (read from the .so constant pool at file offset 0x41884).
bool IsUpgradeAllowed(int type, const Game &g)
{
    switch (type) {
    case 2:   return g.upgradeGateLevel < 0x19;
    case 4:   return g.critChance < 0x5f;                    // +0xc18c
    case 6:   return g.xpRate   < 2.0f;                      // +0xc194
    case 7:   return g.goldRate < 2.0f;                      // +0xc19c
    case 0xd: return g.upRate   < 2.0f;                      // +0xc1a4
    case 0xe: return 100 - std::min(g.armourBreakChance, 100) < 0x5a;
    case 10:  return !SpecialFlagPairSet(g);                 // IsUpgradeAllowed@0x517b8 case 10
    default:  return true;
    }
}

// ItemUpgrade::GetRandomStat @ 0x51db8. Picks the k-th remaining candidate upgrade id
// (k = RandI(0, count-1, RAND_UPGRADE)). Candidate lists verified against the decomp
// inserts; filters in original order. keepDef = decomp param_2 (true keeps id 5 = DEF).
int GetRandomStat(int itemType, bool keepDef, const Game &g)
{
    std::set<int> s;
    switch (itemType) {
    case 0: s = {0, 1, 2, 3, 4, 6, 8, 9};            break;
    case 1: s = {11, 10, 12, 17, 6, 7, 13};          break;
    case 2: s = {11, 14, 10, 12, 15, 17, 18, 19};    break;
    case 3: s = {11, 0, 1, 16, 12, 15, 13};          break;
    default: s = {8, 9, 18, 19, 5, 17, 10};          break;   // type 4
    }
    if (!keepDef) s.erase(5);                       // decomp removes only id 5 (ITEM_STATS.md
                                                    // "remove ids 0..5" was a GUESS — code erases 5)
    if (g.challenge && g.mode == MODE_SPRINT)       // +0x14335 && +0x14330 == 0
        s.erase(15);
    else if (RandI(0, g.blunting, RAND_UPGRADE) != 0)   // +0xc22c gate
        s.erase(15);
    if (!IsUpgradeAllowed(10, g)) s.erase(10);
    if (!IsUpgradeAllowed(2, g))  s.erase(2);
    if (!IsUpgradeAllowed(6, g))  s.erase(6);
    if (!IsUpgradeAllowed(0xd, g)) s.erase(13);
    if (!IsUpgradeAllowed(7, g))  s.erase(7);
    if (!IsUpgradeAllowed(0xe, g)) s.erase(14);
    if (!IsUpgradeAllowed(4, g))  s.erase(4);
    int k = RandI(0, static_cast<int>(s.size()) - 1, RAND_UPGRADE);
    auto it = s.begin();
    for (int i = 0; i < k; ++i) ++it;
    return *it;
}

// Item::ClearStats @ 0x4bac4 — zeroes the 20 stat slots.
void ClearStats(Item &it) { it.stats.fill(0); }

// Item::GenerateStats @ 0x50ee4 — rolls the new item's stats relative to `base`
// (null = fresh default). Probabilities from .rodata (ITEM_STATS.md):
// 0.10 DAT_00051244, 0.15 DAT_00051248, 0.50 DAT_0005124c/000515f4, 0.20 DAT_00051250,
// 0.70 DAT_00051254. All rolls use Rand(0,1,RAND_ITEM).
// NOTE: the analysis notes call decomp case 2 "shield" and case 3 "armour"; the SWIG enum
// (ARMOUR=2, SHIELD=3) is used here — the logic follows the decomp case numbers.
void GenerateStats(Item &it, const Item *base)
{
    ClearStats(it);
    Game &g = *it.game;
    if (base == nullptr) {
        switch (it.type) {
        case WEAPON:  it.stats[0] = 2;  return;
        case HELMET:
        case SHIELD:  it.stats[11] = 1; return;
        case ARMOUR:  it.stats[11] = g.HaveFlaw(FLAW_SLIM) ? 1 : 2; return;   // HaveFlaw(6)
        case ACCESSORY:
            if (SpecialFlagPairSet(g)) return;      // GenerateStats@0x50ee4 case 4 fresh: pretzel -> no stat10
            it.stats[10] = 10;
            return;
        default: return;
        }
    }

    std::set<int> s;                                // S = base's nonzero stat indices
    for (int i = 0; i < ITEM_STAT_SLOTS; ++i)
        if (base->stats[i] > 0) s.insert(i);

    switch (it.type) {
    case WEAPON: {                                  // decomp case 0
        it.stats[0] = base->stats[0] + 1;
        s.erase(0);
        for (int i : s) {
            if (Rand(0.0f, 1.0f, RAND_ITEM) < 0.15f) {
                it.stats[i] = std::max(0, base->stats[i] - GetStatDowngrade(i, g));
                it.stats[0] += 1;                   // extra attack compensation
            } else {
                it.stats[i] = base->stats[i];
            }
        }
        break;
    }
    case HELMET: {                                  // decomp case 1
        it.stats[11] = base->stats[11] + 1;
        s.erase(11);
        bool upgradeUsed = false;
        for (int i : s) {
            if (Rand(0.0f, 1.0f, RAND_ITEM) < 0.20f) {
                if (IsUpgradeAllowed(i, g) && !upgradeUsed &&
                    Rand(0.0f, 1.0f, RAND_ITEM) < 0.50f) {
                    it.stats[i] = base->stats[i] + GetStatUpgrade(i, g);
                    upgradeUsed = true;             // at most ONE upgrade per helmet
                } else {
                    it.stats[i] = std::max(0, base->stats[i] - GetStatDowngrade(i, g));
                    it.stats[11] += 1;
                }
            } else {
                it.stats[i] = base->stats[i];
            }
        }
        break;
    }
    case ARMOUR: {                                  // decomp case 2
        bool a = Rand(0.0f, 1.0f, RAND_ITEM) < 0.20f;   // bVar1 = !a (GenerateStats@0x50ee4)
        it.stats[11] = base->stats[11] + (a ? 2 : 1);
        s.erase(11);
        for (int i : s) {
            if (!a && !(Rand(0.0f, 1.0f, RAND_ITEM) < 0.10f)) {
                it.stats[i] = std::max(0, base->stats[i] - GetStatDowngrade(i, g));
                it.stats[11] += 1;
            } else {
                it.stats[i] = base->stats[i];       // a: plain copy, no downgrades
            }
        }
        break;
    }
    case SHIELD: {                                  // decomp case 3
        it.stats[11] = base->stats[11] + 1;
        s.erase(11);
        for (int i : s) {
            if (i == 0) {
                if (Rand(0.0f, 1.0f, RAND_ITEM) < 0.70f) {
                    it.stats[0] = std::max(0, base->stats[0] - GetStatDowngrade(0, g));
                    it.stats[11] += 1;
                } else {
                    it.stats[0] = base->stats[0];
                }
            } else {
                if (Rand(0.0f, 1.0f, RAND_ITEM) < 0.20f) {
                    it.stats[i] = base->stats[i];
                } else {
                    it.stats[i] = std::max(0, base->stats[i] - GetStatDowngrade(i, g));
                    it.stats[0]  += 1;              // decomp: same draw re-tested, always falls through
                }
            }
        }
        break;
    }
    case ACCESSORY: {                               // decomp case 4 of the SECOND switch @ 0x50ee4
        // (shop/base path). The fresh path (base==nullptr) is the first switch's caseD_4 @ 0x514e4:
        // guard + stat10=10, no RNG.
        // V11: decomp 0x50ee4 lines 466-488 is lower_bound(10)+erase — native REMOVES index 10
        // from S right after setting it (ITEM_STATS.md: "Remove index 10 from S"), so the loop
        // never rolls stat 10 (shop template = {10} only -> loop is empty, 0 draws) and never
        // overwrites stats[10]. Bonus roll breaks when u >= 0.5 (DAT_000515f4), no stream-4 draw.
        // Keeping 10 in s drew one extra RAND_ITEM + a spurious GetRandomStat (stream 4) per
        // shop open and pinned stats[10] at 10 instead of 15 (9 census seeds, item4.mask/statN).
        it.stats[10] = base->stats[10] + GetStatUpgrade(10, g) / 2;   // decomp 0x50ee4 +461
        s.erase(10);                                // decomp 0x50ee4 +466..+488 (lower_bound+erase)
        for (int i : s) {
            if (Rand(0.0f, 1.0f, RAND_ITEM) < 0.10f) {
                it.stats[i] = std::max(0, base->stats[i] - GetStatDowngrade(i, g));
                int r = GetRandomStat(ACCESSORY, false, g);
                it.stats[r] += GetStatUpgrade(r, g);
            } else {
                it.stats[i] = base->stats[i];
            }
        }
        if (!(g.challenge && g.mode == MODE_PRETZEL)) {   // +0x14335 && +0x14330 == 1
            if (!(Rand(0.0f, 1.0f, RAND_ITEM) < 0.50f)) break;   // native: break when u >= 0.5
        }
        // Item+0xa8 = stats[5] (Deserialise1 @0x4da08:78-88: 20 ints at +0x94), not an upgrade
        // count; the original computes keepDef = (stats[5] == 0) — GenerateStats @0x50ee4:552-556
        // (`cVar9 = 1 - stats[5]; if (stats[5] > 1) cVar9 = false`).
        bool keepDef = it.stats[5] == 0;
        int r = GetRandomStat(ACCESSORY, keepDef, g);
        it.stats[r] += GetStatUpgrade(r, g);
        break;
    }
    default: break;
    }
}

} // namespace

// Item::Initialise(ItemType) @ 0x4c0ec + Item::Initialise(Item*) @ 0x515f8.
// Raw value = RandI(min, max, RAND_ITEM) from the per-type range globals. Note the type order:
// ARMOUR (2) uses g_armourRange [240,319] and SHIELD (3) g_shieldsRange [160,239] — read from the
// running original (tools/emu, globals 0x85384 / 0x8537c), not in id order.
// CONFIRMED (V16): no level/turn/difficulty term — Item::Initialise @0x4c0ec and Item::GenerateStats
// @0x50ee4 read none of Game+0xc / +0xc20c / +0x14328; the amount inputs they do read are
// +0xc220, +0xc188, +0xc22c and the mode flags +0x14330/+0x14335 (ITEM_STATS.md).
static const int kItemIdLo[ITEM_TYPE_COUNT] = {0, 80, 240, 160, 320};
static const int kItemIdHi[ITEM_TYPE_COUNT] = {79, 159, 319, 239, 399};

int ItemIdBase(ItemType t) { return kItemIdLo[t]; }

// Game::GetItem @ 0x24610 returns Game+0xc248..0xc258 — the EQUIPPED item of each type, not a
// pristine template: Game::SetItem @ 0x24644 writes those pointers when FoldoutMenu::Process @
// 0x41fc4 confirms a shop offer, and InitNewGame deletes/recreates them. FoldoutMenu::Initialise
// @ 0x42794 calls GetItem(type) and passes it to Item::Initialise as the GenerateStats base, so
// the offer rolls relative to what the player is actually wearing.
// Measured on the original (tools/emu): seed 26 move 56 — the equipped shield has stat11+stat15
// (upgraded earlier), native GenerateStats case 3 therefore runs the loop once and draws one
// RAND_ITEM before the skill roll; a pristine-template base drew zero and shifted every later
// stream-2 value (randSeed2 diverged). With the equipped base seeds 1 2 6 26 31337 are 80/80
// identical under tools/emu/diff.py; with templates they are not.
Item *Game::EquippedRef::get() const { return g->items[t].get(); }

void Game::InitBaseItems()
{
    for (int t = 0; t < 5; ++t) baseItems[t] = {this, t};
}

// Item::GenerateName @ 0x4c35c. Only the RNG draws and the saved indices are rebuilt (the text is
// presentation). Prefix table g_itemPrefixes has 148 entries; the label tables are 13 id ranges with
// these sizes (dumped from the original after Item::InitialiseNames @ 0x4dc24).
static void GenerateName(Item &it, const Item *base)
{
    static const int kPrefixCount = 148;
    static const struct { int lo, hi, labels; } kLabels[] = {
        {0, 20, 15}, {20, 40, 12}, {40, 50, 13}, {50, 60, 12}, {60, 80, 15}, {80, 160, 17},
        {160, 240, 16}, {240, 320, 17}, {320, 340, 14}, {340, 360, 11}, {360, 370, 14},
        {370, 380, 16}, {380, 400, 16}};
    if (!base) { it.nameIndex[0] = it.nameIndex[1] = -1; }
    else {
        int p = base->nameIndex[0];
        if (p < kPrefixCount - 12) {
            p = RandI(p + 1, p + 10, RAND_ITEM);
            it.nameIndex[0] = it.nameIndex[1] = p;
        } else {
            it.nameIndex[0] = p;
            it.nameIndex[1] = RandI(base->nameIndex[0], kPrefixCount - 1, RAND_ITEM);
        }
    }
    for (const auto &r : kLabels)
        if (r.lo <= it.id && it.id < r.hi) { it.label = RandI(0, r.labels - 1, RAND_ITEM); return; }
}

void Item::Initialise(ItemType t, const Item *replacing)
{
    type = t;
    id = RandI(kItemIdLo[t], kItemIdHi[t], RAND_ITEM);
    // Upgrade::SetIcon(id%20, id/20, 0x400) — presentation, not rebuilt.
    GenerateName(*this, replacing);
    GenerateStats(*this, replacing);
    // MakeStats @ 0x4cbe0 — display text, not rebuilt.
}

// Item::AddUpgrade @ 0x4dc08 — stats[upgrade.stat] += upgrade.value (no clamp in the
// original), then MakeStats (text, not rebuilt).
void Item::AddUpgrade(const ItemUpgrade &u)
{
    stats[u.stat] += u.value;
    upgrades.push_back(u);   // rebuild-side bookkeeping only; the original AddUpgrade @0x4dc08
                             // writes stats[stat] += value + MakeStats and keeps NO counter
                             // (Item+0xa8 is stats[5], not an upgrade count).
}

// ItemUpgrade::Initialise @ 0x523c8 + InitStats @ 0x51ae4 (amount only; the description
// lines WriteStatInt/Percent/Float are UI text, not rebuilt).
ItemUpgrade Game::RollItemUpgrade(const Item &item)
{
    // keepDef = (Item+0xa8 == 0) with Item+0xa8 = stats[5]; same expression as GenerateStats —
    // ItemUpgrade::Initialise @0x523c8:7-15 (`cVar2 = 1 - stats[5]; if (stats[5] > 1) false`).
    bool keepDef = item.stats[5] == 0;
    ItemUpgrade u;
    u.stat  = GetRandomStat(item.type, keepDef, *this);
    u.value = GetStatUpgrade(u.stat, *this);
    return u;
}

} // namespace dr
