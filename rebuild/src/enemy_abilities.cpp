// Enemy ability system: the 21 Game::* helpers called by Tile::ActivateAbility @ 0x64c78.
// Sources: analysis/combat/ENEMY_STATS.md + the decomp Game/<Name>@*.c files for exact numbers.
// Flares/labels/sounds go through Platform (presentation only). Token flights (stolen coin,
// thrown skull) are modelled as the V23 tokenQueue (no animation): the queued {type, tokenType,
// value} lands through AddResource exactly as Game::Process @ 0x339ac does.
#include "dr/game.h"
#include "dr/character_class.h"
#include "dr/item.h"
#include "dr/rng.h"
#include <algorithm>
#include <vector>

namespace dr {

// ---- file-local helpers (Tile methods reduced to their state changes; Quad/UV code dropped) ----

// Tile::SetFlameVisibility @ 0x62de8 — on: flameDamage(+0x268) = attack if fresh, += attack if
// already burning; flaming(+0x266)=1; thaws frozen(+0x265)=0. param_3 (+0x267) not modelled.
static void SetFlame(Tile &t, bool on, int attack, bool flag)
{
    if (on) {
        t.flameDamage = t.flaming ? t.flameDamage + attack : attack;
        t.flamePulse = flag;        // +0x267 = param_3 (0x62de8:24); V24: gates the press flame preview
        t.flaming = true;
        t.frozen = false;
    } else {
        t.flaming = false;
    }
}

// Tile::SetFreezeVisibility @ 0x62e94 — frozen(+0x265) = on; freezing extinguishes flaming(+0x266)=0.
static void SetFreeze(Tile &t, bool on)
{
    t.frozen = on;
    if (on) t.flaming = false;
}

// Tile::MakeAcidPotion @ 0x64334 — acidPotion(+0x243) = 1 (UV swap is render).
static void MakeAcid(Tile &t) { t.acidPotion = true; }

// Tile::MakeBroken @ 0x64a80 — broken(+0x26c) = 1, only for shield(3)/sword(4).
static void MakeBroken(Tile &t)
{
    if (t.type == TILE_SHIELD || t.type == TILE_SWORD) t.broken = true;
}

// Game::GetNormalPotionCount @ 0x24804 — potions (type 0) that are not explosive/mana/acid.
int Game::GetNormalPotionCount() const
{
    int n = 0;
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            const Tile &t = board[c][r];   // At() has no const overload
            if (t.type == TILE_POTION && !t.explosivePotion && !t.manaPotion && !t.acidPotion)
                ++n;
        }
    return n;
}

// ItemUpgrade::GetStatUpgrade @ 0x51888 — file-local copy (items.cpp's is anonymous-namespace,
// V11-owned). +0xc220 = upgradeGateLevel, +0xc188 = armourBreakChance (ITEM_STATS.md).
static int StatUpgradeLocal(int type, const Game &g)
{
    switch (type) {
    case 0: case 1: case 5: case 6: case 7: case 8: case 9:
    case 0xb: case 0xc: case 0xd: case 0xf: case 0x10: case 0x12: case 0x13:
        return 1;
    case 2: {
        int x = g.upgradeGateLevel;
        if (x < 0x12 && x < 0xf) return 5;      // (x<0x17 && x<0x15) && (x<0x12 && x<0xf)
        if (x < 0x15) return 3;
        return 2;                               // fallthrough to case 3
    }
    case 3:  return 2;
    case 4:  return 10;                         // break -> return 10
    case 10: return (g.challenge && g.mode == MODE_PRETZEL) ? 0 : 10;
    case 0xe: {
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

// ItemUpgrade::GetStatDowngrade @ 0x5193c — file-local copy (see note above). Types 2 and 0xe use
// their own tables; every other type delegates to GetStatUpgrade @ 0x51888.
static int GetStatDowngrade(int type, const Game &g)
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
            return 10;
        }
        return 10;
    }
    return StatUpgradeLocal(type, g);
}

// Item::ReduceStat @ 0x4dbe0 — stat -= ItemUpgrade::GetStatDowngrade(stat, game), clamped >= 0
// (decomp: `iVar2 - iVar1 & ~(iVar2 - iVar1 >> 0x1f)`), then MakeStats (presentation — not modelled).
static void ReduceStat(Item &item, const Game &g, int slot)
{
    item.stats[static_cast<size_t>(slot)] =
        std::max(0, item.stats[static_cast<size_t>(slot)] - GetStatDowngrade(slot, g));
}

// Pick a random element of pool. The original collects candidates in a std::set<Tile*> and picks
// the RandI-th node by in-order traversal = ascending Tile* address. The original's grid is
// Game+0x5fcc + col*0x270 + row*0xea0 (Tile stride 0x270, ROW stride 0xea0 = 6*0x270), so address
// order is ROW-major: row outer, col inner. Collect candidate pools with CollectPool() (row outer)
// so pool[i] == the original's i-th set node. (V10: the old col-major order picked the wrong tile
// in BreakRandomShieldSword @ 0x2e55c seed 9 and HealOrBless @ 0x34614 seed 15.)
static size_t RandPick(size_t n) { return static_cast<size_t>(RandI(0, static_cast<int>(n) - 1, RAND_OTHER)); }

// Collect At(c,r) tiles in the original's Tile* address order (row 0..5 outer, col 0..5 inner).
// keep(At(c,r)) decides membership.
template <typename F>
static std::vector<Tile *> CollectPool(Game &g, F keep)
{
    std::vector<Tile *> pool;
    for (int r = 0; r < BOARD_SIZE; ++r)
        for (int c = 0; c < BOARD_SIZE; ++c) {
            Tile &x = g.At(c, r);
            if (keep(x, c, r)) pool.push_back(&x);
        }
    return pool;
}

// ---- abilities ----

// Game::RunAwayEnemy @ 0x300d4 (Golden, fuse done) — the tile flees: MatchTile(t, NONE) clears it,
// gravity refills, then every tile's inChain(+0x8a) flag is cleared (chain draw reset).
void Game::RunAwayEnemy(Tile &t)
{
    fx().AddFlare(&t, 1);
    fx().QueueSound(SOUND_JUMP);            // 0x23
    MatchTile(t, TILE_NONE);
    MoveMatchedTiles();
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r)
            At(c, r).inChain = false;       // +0x8a = 0 for all 36 tiles
}

// Game::ExplodeEnemy @ 0x2b2e4 (Kamikaze, fuse done) — clears the per-chain clear counters
// (+0xc36c..+0xc37c) and the once-per-chain explosion-sound flag, then MatchTile(t, NONE) kills
// the kamikaze (its MatchTile path runs the 3x3 blast, see chain.cpp Blast3x3) and sets the
// player's HP to half its CURRENT HP (min 1) — g_enemyText says "half your max health", the code
// uses +0xc158 (current).
void Game::ExplodeEnemy(Tile &t)
{
    for (int i = 0; i < TILE_TYPE_COUNT; ++i) clearedThisChain[i] = 0;
    explosionSoundPlayed = false;           // +0x142f4
    MatchTile(t, TILE_NONE);
    fx().AddFlare(&t, 1);
    int v = hp / 2;
    if (v < 1) v = 1;
    hp = v;                                 // +0xc158
    fx().OnHealthChanged(hp);
}

// Game::MimicDefence @ 0x2e84c (Doppelganger) — armour(+0xac) := player armour (+0xc164), absolute.
void Game::MimicDefence(Tile &t)
{
    t.defence = armour;
    fx().AddLabel(0x14, &t, 0);
    fx().QueueSound(SOUND_BOOST);           // 0x17
}

// Game::Resurrect @ 0x2d350 (Lich) — presentation only: the caller (ActivateAbility case 32) has
// already rerolled GenerateStats/UpdateEnemyUV/UpdateValues. Sounds 0x16+0xe, flare, label 0x26.
void Game::Resurrect(Tile &t)
{
    fx().QueueSound(SOUND_SHATTER);         // 0x16
    fx().QueueSound(SOUND_SKULL);           // 0xe
    fx().AddFlare(&t, 1);
    fx().AddLabel(0x26, &t, 0);
}

// Game::FleeUp @ 0x2eda0 (Fleeing) — moves up 1 tile; at row 0 it leaves the board (MatchTile NONE).
// The "damages you when pushed down" part is not here (it lives in the gravity path).
void Game::FleeUp(Tile &t)
{
    fx().AddLabel(0x25, &t, 0);
    if (t.row == 0) {
        MatchTile(t, TILE_NONE);
        fx().AddFlare(&t, 1);
    } else {
        SwapTiles(t.col, t.row - 1, t.col, t.row);
        fx().AddFlare(&t, 1);
        fx().AddFlare(&At(t.col, t.row), 1);   // flare on the tile it swapped with
        fx().QueueSound(SOUND_JUMP);           // 0x23
    }
}

// Game::TeleportEnemy @ 0x29ad8 (Unstable, fuse done; takes no Tile*) — marks EVERY unmatched
// tile matched (+0x88=1) so the next gravity pass refills the whole board ("teleport").
// GUESS: the refill itself happens on the next MoveMatchedTiles call (not called here).
// If the player's class mortal enemy is Unstable (Changeling): firstBoard(+0xc380)=1 AND
// firstBoardAvoidSword(+0xc381)=1 (refill avoids swords) + achievement 38; else firstBoard=1 only.
void Game::TeleportEnemy()
{
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &t = At(c, r);
            if (!t.matched) {
                t.matched = true;
                fx().AddFlare(&t, 1);
            }
        }
    firstBoard = true;
    firstBoardAvoidSword = (ActiveClass().MortalEnemy() == ENEMY_UNSTABLE);
    if (firstBoardAvoidSword)
        fx().QueueAchievement(ACH_UNFORESEEN_CONSEQUENCES, 100.0f);   // 0x26 @ 100
    fx().QueueSound(SOUND_TELEPORT);        // 0x15
}

// Game::StabAttack @ 0x2f250 (Assassin) — if the player's class mortal enemy is Assassin (Faun):
// when not in the bottom row it descends — the tile below must not be a special skull (then the
// turn does nothing), and the tile below is destroyed (MatchTile NONE). In ALL cases only a row-5
// assassin stabs (decomp @ 0x2f288 `if (row != 5) return;` runs after the Faun block too): direct
// health damage = 2 x attack (ignores armour), clamped at 0.
void Game::StabAttack(Tile &t)
{
    if (ActiveClass().MortalEnemy() == ENEMY_ASSASSIN) {   // GetMortalEnemyType == 0x10 @ 0x2f25c
        if (t.row != 5) {
            Tile &below = At(t.col, t.row + 1);
            // K20 (Y7, disassembly-checked; StabAttack @0x2f250 asm ELF 0x1f2a0/0x1f35a): the
            // test reads Game+tileoff+0x606c = tile+0xa0
            // (type == 1) and Game+tileoff+0x6228 = tile+0x25c (enemyType != 0) — 0x5fcc+0x25c
            // = 0x6228, NOT tile+0x22c (0x5fcc+0x22c = 0x61f8, zero hits in decomp).
            // tile+0x22c is the glow Quad (+0x1c0) rotation angle = Quad+0x6c: 0.0f in
            // Quad::Quad @0x5b170, advanced Tile::ProcessEffects @0x63b84
            // (angle += dt * +0x238 while glow flag +0x234), drawn Quad::Render @0x5b528
            // (glRotatef), copied operator= @0x27cd4:88 / copy ctor @0x28800:101 —
            // render-only, no game-logic reader. The modelled "special skull" is correct.
            if (below.type == TILE_SKULL && below.enemyType != ENEMY_NORMAL)
                return;
            MatchTile(below, TILE_NONE);
        }
    }
    if (t.row != 5) return;                                 // decomp @ 0x2f288: only row 5 stabs
    fx().QueueSound(SOUND_ATTACK_HIT);      // 0x1b
    fx().QueueSound(SOUND_DAMAGE);          // 0x11
    fx().AddLabel(0x13, &t, 0);
    hp = std::max(0, hp - 2 * t.GetAttack());   // +0xc158, no armour
    fx().OnHealthChanged(hp);
    fx().AddLabel(2, &t, 2 * t.GetAttack());    // label 2 at the poison position, value 2*atk
}

// Game::HealOrBless @ 0x34740 (Healer) — if any other monster is damaged: heal up to 2 of them to
// full; else bless up to 2 other monsters (+1 attack each). Overload @ 0x34614 picks random set
// members recursively (count 2). Sound 0x24 = BLESS in both branches.
static void HealOrBlessPick(Game &g, std::vector<Tile *> &pool, bool heal, int count)
{
    while (count > 0 && !pool.empty()) {
        Tile *p = pool[RandPick(pool.size())];
        if (heal) {
            p->health = p->maxHealth;       // +0xb0 := +0xb4
            g.fx().AddLabel(0xc, p, 0);
        } else {
            p->attack += 1;                 // +0xa8 += 1
            g.fx().AddLabel(0xb, p, 0);
        }
        g.fx().AddFlare(p, 1);
        pool.erase(std::find(pool.begin(), pool.end(), p));
        --count;
    }
}

void Game::HealOrBless(Tile &t)
{
    std::vector<Tile *> all = CollectPool(*this, [&](const Tile &x, int, int) {
        return &x != &t && !x.matched && x.type == TILE_SKULL;
    });
    std::vector<Tile *> damaged;
    for (Tile *p : all) if (p->health < p->maxHealth) damaged.push_back(p);
    if (!damaged.empty()) {
        fx().QueueSound(SOUND_BLESS);       // 0x24
        HealOrBlessPick(*this, damaged, true, 2);
    } else if (!all.empty()) {
        fx().QueueSound(SOUND_BLESS);
        HealOrBlessPick(*this, all, false, 2);
    }
}

// Game::MakeAcidPotion @ 0x2ea40 (Acidic) — picks the RandI-th normal potion (column-major scan)
// and turns it into an acid potion. Label 0xe + sound 0x18 only when at least one potion exists.
void Game::MakeAcidPotion(Tile &t)
{
    int n = GetNormalPotionCount();
    int idx = RandI(0, n - 1, RAND_OTHER);  // n==0 -> RandI(0,-1): -1..0, scan then finds nothing
    if (n > 0) {
        fx().AddLabel(0xe, &t, 0);
        fx().QueueSound(SOUND_EXPLOSIVE);   // 0x18
    }
    if (idx < 0) return;
    for (int c = 0; c < BOARD_SIZE && idx >= 0; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &x = At(c, r);
            if (x.type == TILE_POTION && !x.explosivePotion && !x.manaPotion && !x.acidPotion) {
                if (idx == 0) {
                    MakeAcid(x);
                    fx().AddFlare(&x, 1);
                    idx = -1;
                    break;
                }
                --idx;
            }
        }
}

// Game::IgniteTiles @ 0x29be0 (Flaming) — ignites the 3x3 block around the flaming tile
// (col-1..col+1, row-1..row+1) EXCLUDING the centre (the flaming tile itself): the decomp's two
// loops run iVar6/iVar3 = -1..1 and skip the iteration where both are 0 (the centre). It skips
// matched tiles (*pGVar5 == 0 at tile+0x88) and other Flaming skulls (type 1 && enemyType 7,
// pGVar5+0x18 / +0x1d4). SetFlameVisibility(true, this->GetAttack(), false): flameDamage += attack,
// thaws the tile. No label/sound (only AddFlare per tile + the +0xc150/+0xc154 render colours).
void Game::IgniteTiles(Tile &t)
{
    for (int dc = -1; dc <= 1; ++dc)
        for (int dr = -1; dr <= 1; ++dr) {
            if (dc == 0 && dr == 0) continue;               // centre (the flaming tile) skipped
            int c = t.col + dc, r = t.row + dr;
            if (c < 0 || c >= BOARD_SIZE || r < 0 || r >= BOARD_SIZE) continue;
            Tile &x = At(c, r);
            if (x.matched) continue;                        // +0x88
            if (x.type == TILE_SKULL && x.enemyType == ENEMY_FLAMING) continue;
            SetFlame(x, true, t.GetAttack(), false);
            fx().AddFlare(&x, 1);
        }
}

// Game::FreezeTiles @ 0x2e898 (Freezing) — freezes up to 2 random tiles: candidates are unmatched,
// not frozen, and not a special enemy (skull with N != 0). Label 0x11, sound 0x14 = FREEZE.
void Game::FreezeTiles(Tile &t)
{
    std::vector<Tile *> pool = CollectPool(*this, [](const Tile &x, int, int) {
        if (x.matched || x.frozen) return false;
        return !(x.type == TILE_SKULL && x.enemyType != ENEMY_NORMAL);
    });
    int count = 2;
    while (count > 0 && !pool.empty()) {
        Tile *p = pool[RandPick(pool.size())];
        SetFreeze(*p, true);
        fx().AddFlare(p, 1);
        pool.erase(std::find(pool.begin(), pool.end(), p));
        --count;
    }
    fx().AddLabel(0x11, &t, 0);
    fx().QueueSound(SOUND_FREEZE);          // 0x14
}

// Game::ConvertSwordToSkull @ 0x2eb4c (Overseer) — picks the RandI-th sword tile (column-major)
// and SetType(SKULL, turn, difficulty) on it; oneHitSkulls (+0x1433d) -> health = 1.
void Game::ConvertSwordToSkull(Tile &t)
{
    int n = GetTileCount(TILE_SWORD);
    int idx = RandI(0, n - 1, RAND_OTHER);
    if (n > 0) {
        fx().AddLabel(0x12, &t, 0);
        fx().QueueSound(SOUND_SKULL);       // 0xe
    }
    if (idx < 0) return;
    for (int c = 0; c < BOARD_SIZE && idx >= 0; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &x = At(c, r);
            if (x.type == TILE_SWORD) {
                if (idx == 0) {
                    x.SetType(TILE_SKULL, turn, difficulty);
                    if (oneHitSkulls) x.health = 1;         // +0xb0 = 1
                    fx().AddFlare(&x, 1);
                    fx().AddLabel(0x12, &x, 0);
                    idx = -1;
                    break;
                }
                --idx;
            }
        }
}

// Game::ConvertRandomTiles @ 0x2e6bc (Alchemist) — converts 2 random tiles: candidates are
// unmatched tiles that are not special enemies (skull with N != 0); each picked tile gets
// RandomiseTile (full re-roll). Label 0x15, sound 0x18 = EXPLOSIVE.
void Game::ConvertRandomTiles(Tile &t)
{
    std::vector<Tile *> pool = CollectPool(*this, [](const Tile &x, int, int) {
        if (x.matched) return false;
        return !(x.type == TILE_SKULL && x.enemyType != ENEMY_NORMAL);
    });
    int count = 2;
    while (count > 0 && !pool.empty()) {
        Tile *p = pool[RandPick(pool.size())];
        RandomiseTile(*p);
        fx().AddFlare(p, 1);
        pool.erase(std::find(pool.begin(), pool.end(), p));
        --count;
    }
    fx().AddLabel(0x15, &t, 0);
    fx().QueueSound(SOUND_EXPLOSIVE);       // 0x18
}

// Game::BreakRandomShieldSword @ 0x2e55c (Trampling) — breaks 1 random shield, or a sword that is
// not an arrow (arrow swords are protected, GUESS). Candidates: unmatched, not already broken.
// Label 0x16, sound 0x10 = ARMOUR_BREAK.
void Game::BreakRandomShieldSword(Tile &t)
{
    std::vector<Tile *> pool = CollectPool(*this, [](const Tile &x, int, int) {
        if (x.matched || x.broken) return false;
        if (x.type == TILE_SWORD) return !x.arrow;
        return x.type == TILE_SHIELD;
    });
    if (pool.empty()) return;
    Tile *p = pool[RandPick(pool.size())];
    MakeBroken(*p);
    fx().AddFlare(p, 1);
    fx().AddLabel(0x16, &t, 0);
    fx().QueueSound(SOUND_ARMOUR_BREAK);    // 0x10
}

// Game::StealGold @ 0x34a68 (Thief) — steals up to 2 coin tiles (overload @ 0x348e8: marks them
// matched so gravity removes them; a coin token flies to the thief, presentation only). With no
// coin tiles in play and gold > 0: 2 iterations, each takes 1 gold from the inventory; if the
// player's class mortal enemy is Thief (Sauren) each iteration also hits the player for the
// thief's attack (a potion token flies to the potion bar, presentation). Label 0xd.
void Game::StealGold(Tile &t)
{
    std::vector<Tile *> coins = CollectPool(*this, [](const Tile &x, int, int) {
        return x.type == TILE_COIN && !x.matched;
    });
    fx().AddLabel(0xd, &t, 0);
    if (!coins.empty()) {
        int count = 2;
        while (count > 0 && !coins.empty()) {
            Tile *p = coins[RandPick(coins.size())];
            p->matched = true;              // +0x88 = 1: coin is gone on the next gravity pass
            // 0x348e8:39 InitialiseFromTile(token, the stolen coin tile, 2, 0, 0)
            CosmeticToken(2, static_cast<int>(p->type), 0);
            fx().AddFlare(p, 1);
            coins.erase(std::find(coins.begin(), coins.end(), p));
            --count;
        }
    } else if (gold > 0) {
        for (int i = 0; i < 2; ++i) {
            if (gold < 1) break;
            // 0x34a68:89 InitialiseFromTile(token, this+0x5fcc = board(0,0), 2, 0, 0), then gold-- at :112
            CosmeticToken(2, static_cast<int>(board[0][0].type), 0);
            --gold;                         // +0xc178
            if (ActiveClass().MortalEnemy() == ENEMY_THIEF) {
                // 0x34a68:122 InitialiseFromTile(token, board(0,0), 0, 0, 0)
                CosmeticToken(0, static_cast<int>(board[0][0].type), 0);
                hp -= t.GetAttack();        // +0xc158 -= attack (no clamp in the original)
                fx().OnHealthChanged(hp);   // Interface::UpdateHealthBar @ 0x34a68:155-159
            }
        }
    }
}

// Game::JumpEnemy @ 0x2f370 (Chaotic) — jumps to a random tile: candidates are unmatched, not
// special enemies, not itself. SwapTiles(enemy, target) moves it; the tile left at the enemy's old
// cell is consumed (MatchTile NONE). Label 0xf at the landing tile, sound 0x23 = JUMP.
void Game::JumpEnemy(Tile &t)
{
    std::vector<Tile *> pool = CollectPool(*this, [&](const Tile &x, int c, int r) {
        if (x.matched) return false;
        if (x.type == TILE_SKULL && x.enemyType != ENEMY_NORMAL) return false;
        return !(c == t.col && r == t.row);
    });
    if (pool.empty()) return;
    Tile *d = pool[RandPick(pool.size())];
    const int sc = t.col, sr = t.row;
    SwapTiles(d->col, d->row, sc, sr);
    MatchTile(At(sc, sr), TILE_NONE);       // the tile swapped into the old cell is consumed
    fx().AddFlare(&At(sc, sr), 1);
    fx().AddFlare(d, 1);
    fx().AddLabel(0xf, d, 0);
    fx().QueueSound(SOUND_JUMP);            // 0x23
}

// Game::LowerSkulls @ 0x2f154 (Greasy) — per column, scans upward from row 4 for the first skull
// whose tile below is not a skull (skipping the greasy tile itself); the tile below it is cleared
// (MatchTile NONE) so the skull drops one row. One skull per column per turn.
// Label 0x17, sound 5 = POTION_START.
void Game::LowerSkulls(Tile &t)
{
    for (int c = 0; c < BOARD_SIZE; ++c) {
        for (int r = 4; r >= 0; --r) {
            Tile &x = At(c, r);
            if (x.type != TILE_SKULL) continue;
            if (c == t.col && r == t.row) continue;          // the greasy tile itself
            if (At(c, r + 1).type == TILE_SKULL) continue;   // blocked below: keep scanning up
            Tile &gap = At(c, r + 1);
            MatchTile(gap, TILE_NONE);
            fx().AddFlare(&gap, 1);
            break;
        }
    }
    fx().AddLabel(0x17, &t, 0);
    fx().QueueSound(SOUND_POTION_START);    // 5
}

// Shared body of AttractSkulls @ 0x2e22c (Alpha) and AttractShieldsSwords @ 0x2defc (Magnetic):
// move every tile of one kind (Chebyshev distance > 1 from the magnet) to a random free tile in
// the 3x3 ring around the magnet, pairing random source with random target until one side runs
// out. Sources are swapped (SwapTiles) onto the targets; each source/target is used once.
static void AttractTo(Game &g, Tile &m, const std::vector<Tile *> &srcs,
                      const std::vector<Tile *> &spots)
{
    std::vector<Tile *> s = srcs, d = spots;
    if (s.empty() || d.empty()) return;
    while (!s.empty() && !d.empty()) {
        Tile *a = s[RandPick(s.size())];
        Tile *b = d[RandPick(d.size())];
        g.SwapTiles(a->col, a->row, b->col, b->row);
        g.fx().AddFlare(&g.At(b->col, b->row), 1);   // flare at the destination cell
        g.fx().AddFlare(&g.At(a->col, a->row), 1);   // and at the source cell
        s.erase(std::find(s.begin(), s.end(), a));
        d.erase(std::find(d.begin(), d.end(), b));
    }
}

// Game::AttractSkulls @ 0x2e22c (Alpha) — sources: unmatched skulls more than 1 tile away;
// targets: unmatched non-skull tiles in the 3x3 around the alpha (not the alpha itself).
// Label 0x18, sound 0xe = SKULL.
void Game::AttractSkulls(Tile &t)
{
    std::vector<Tile *> srcs = CollectPool(*this, [&](const Tile &x, int c, int r) {
        return !x.matched && x.type == TILE_SKULL && (std::abs(c - t.col) > 1 || std::abs(r - t.row) > 1);
    });
    if (srcs.empty()) return;
    std::vector<Tile *> spots;
    for (int dr = -1; dr <= 1; ++dr)
        for (int dc = -1; dc <= 1; ++dc) {
            int c = t.col + dc, r = t.row + dr;
            if (c < 0 || c >= BOARD_SIZE || r < 0 || r >= BOARD_SIZE) continue;
            Tile &x = At(c, r);
            if (x.matched || (dc == 0 && dr == 0)) continue;
            if (x.type == TILE_SKULL) continue;
            spots.push_back(&x);
        }
    AttractTo(*this, t, srcs, spots);
    if (spots.empty()) return;
    fx().AddLabel(0x18, &t, 0);
    fx().QueueSound(SOUND_SKULL);           // 0xe
}

// Game::AttractShieldsSwords @ 0x2defc (Magnetic) — same, sources: unmatched swords/shields far
// away; targets: unmatched tiles in the 3x3 that are neither sword nor shield. Label 0x19,
// sound 0x17 = BOOST.
void Game::AttractShieldsSwords(Tile &t)
{
    std::vector<Tile *> srcs = CollectPool(*this, [&](const Tile &x, int c, int r) {
        if (x.matched) return false;
        if (x.type != TILE_SWORD && x.type != TILE_SHIELD) return false;
        return std::abs(c - t.col) > 1 || std::abs(r - t.row) > 1;
    });
    if (srcs.empty()) return;
    std::vector<Tile *> spots;
    for (int dr = -1; dr <= 1; ++dr)
        for (int dc = -1; dc <= 1; ++dc) {
            int c = t.col + dc, r = t.row + dr;
            if (c < 0 || c >= BOARD_SIZE || r < 0 || r >= BOARD_SIZE) continue;
            Tile &x = At(c, r);
            if (x.matched || (dc == 0 && dr == 0)) continue;
            if (x.type == TILE_SWORD || x.type == TILE_SHIELD) continue;
            spots.push_back(&x);
        }
    AttractTo(*this, t, srcs, spots);
    if (spots.empty()) return;
    fx().AddLabel(0x19, &t, 0);
    fx().QueueSound(SOUND_BOOST);           // 0x17
}

// Game::ThrowSkull @ 0x2eeb0 (Brute) — picks a random NORMAL skull (unmatched, N == 0, not in a
// drawn chain) and kills it (MatchTile NONE, decomp MatchTile(this, tile, 7)). The original then
// pushes a thrown-skull token (Token::InitialiseFromTile(tile, 1, 0, 3): token+0x78 = 1,
// token+0x7c = 3, token+0x98 = tile+0xb0 = the skull's health; ThrowSkull@0x2eeb0:124-131) onto the
// +0xb8d4 token vector; Game::Process @ 0x339ac:165 lands it via AddResource(1, 3, health) =
// TILE_SKULL + TOKEN_TRAP -> hp -= health (AddResource @ 0x32210). Queue the token (V23 queue);
// DrainTokens applies the damage. Label 0x1f, sound 0x1a = ATTACK_MISS.
void Game::ThrowSkull(Tile &t)
{
    std::vector<Tile *> pool;
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &x = At(c, r);
            if (!x.matched && x.type == TILE_SKULL && x.enemyType == ENEMY_NORMAL && !x.inChain)
                pool.push_back(&x);   // ThrowSkull @0x2eeb0:41 gate is tile+0x8a == 0
        }
    if (pool.empty()) return;
    Tile *p = pool[RandPick(pool.size())];
    MatchTile(*p, TILE_NONE);
    CosmeticToken(1, static_cast<int>(p->type), 3);   // 0x2eeb0:128 InitialiseFromTile(tile,1,0,3) = 4 draws
    tokenQueue.push_back({static_cast<int>(TILE_SKULL), 3, p->health});   // TOKEN_TRAP, value = health
    fx().AddFlare(p, 1);
    fx().AddLabel(0x1f, &t, 0);
    fx().QueueSound(SOUND_ATTACK_MISS);     // 0x1a
}

// Game::DestroyUpgrade @ 0x2ecb8 (Devourer, fuse done) — picks a random equipped item, collects
// its stat slots with value > 0, and reduces one of them (Item::ReduceStat). Achievement 39
// (NOM_NOM) @ 100 when a stat is destroyed. Label 0x20, sound 0x16 = SHATTER, then UpdateStats.
void Game::DestroyUpgrade(Tile &t)
{
    if (items.empty()) return;              // original assumes >= 1 item (would read a bad index)
    Item &it = *items[RandPick(items.size())];
    std::vector<int> slots;
    for (int i = 0; i < ITEM_STAT_SLOTS; ++i)
        if (it.stats[static_cast<size_t>(i)] > 0)
            slots.push_back(i);
    if (!slots.empty()) {
        ReduceStat(it, *this, slots[RandPick(slots.size())]);
        fx().QueueAchievement(ACH_NOM_NOM, 100.0f);   // 0x27 @ 100
    }
    fx().AddLabel(0x20, &t, 0);
    fx().QueueSound(SOUND_SHATTER);         // 0x16
    UpdateStats();
}

// ---------------------------------------------------------------------------
// Game::AddRevengeHealth @ 0x2d294 — ENEMY_VENGEFUL (21) passively gains this move's
// kill count (Game+0x14d90, reset per move by ProcessInput @0x330c0:257) as attack.
// Called from ProcessInput :286 right after MatchTiles when killsCounted > 0.
// ---------------------------------------------------------------------------
void Game::AddRevengeHealth()
{
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &t = At(c, r);
            if (t.matched || t.type != TILE_SKULL || t.enemyType != ENEMY_VENGEFUL) continue;
            t.attack += killsCounted;                        // +0xa8 += +0x14d90
            // Tile::UpdateText — rendering, not modelled
            fx().AddLabel(0x1b, &t, killsCounted);           // AddSimpleLabel(0x1b, pos, +0x14d90, colour 0x23)
            fx().QueueSound(SOUND_SKULL);                    // QueueSound(this, 0xe)
        }
}

} // namespace dr
