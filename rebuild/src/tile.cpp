// Tile system: Tile::SetType / GenerateStats / ActivateAbility / Promote and CharacterClass::MortalEnemy.
// Sources: analysis/combat/ENEMY_STATS.md, analysis/board/TILE_SKETCH.md, analysis/progression/CLASS_SKETCH.md,
// plus the decomp files Tile/{SetType,GenerateStats,ActivateAbility,Promote}@*.c for exact numbers.
// Label/atlas/quad code (UpdateText, UpdateCountdown, UpdateEnemyUV, UpdateValues) is render-only and is
// reduced to no-ops that keep the cosmetic RNG stream in step.
#include "dr/game.h"
#include "dr/character_class.h"
#include "dr/rng.h"
#include <cmath>
#include <algorithm>

namespace dr {

// Tile::UpdateCountdown @ 0x63030 — sprintf countdown (+0x260) into the countdown label. UI only.
static void UpdateCountdown(Tile &t) { (void)t; }

// Tile::UpdateValues @ 0x63a88 — UpdateText x3 + number-text size (+0xb8) + UpdateCountdown. UI only.
static void UpdateValues(Tile &t) { (void)t; }

// Tile::UpdateEnemyUV @ 0x64b04 — base-quad UV (render) plus the mortal-enemy glow flag +0x234 and its
// pulse speed +0x238 = Rand(30,45,RAND_COSMETIC) (0x64b04:76 Rand(0x41f00000,0x42340000,0)).
// Only the cosmetic RNG draw is modelled (V24: value fixed 15->30; the draw count was already 1).
// Callers in the original: Promote @0x64e3c, DemoteSpecialEnemy @0x2a34c:29 (BEFORE the +0x25c=0
// demotion), AttackSkull @0x2f630:34 (0x20 strip), ActivateAbility @0x64c78:97 (Lich), Deserialise2 (load).
void UpdateEnemyUVDraw(Tile &t)
{
    if (t.game && t.enemyType != ENEMY_NORMAL && t.enemyType == t.game->ActiveClass().MortalEnemy()) {
        Rng0Trace("enemyuv", 1);
        Rand(30.0f, 45.0f, RAND_COSMETIC);   // glow on: speed roll (RAND_COSMETIC stream)
    }
}

static void UpdateEnemyUV(Tile &t) { UpdateEnemyUVDraw(t); }

static void PromoteInternal(Tile &t, int turn, Difficulty difficulty, bool flag, EnemyType forced);

// CharacterClass::GetMortalEnemyType @ 0x35f34 — race -> that race's "mortal enemy".
// Table: 10 ints at .rodata 0x5b298 (analysis/data/TABLES.md), indexed by RaceType; race >= 10 -> 0.
// Human None, Elf Poisonous, Dwarf Chaotic, Sauren Thief, Faun Assassin, Orc Corrosive,
// Halfling Invisible, Werewolf Vampire, Dragonkin Freezing, Changeling Unstable.
EnemyType CharacterClass::MortalEnemy() const
{
    static const int kMortalEnemy[10] = { 0, 9, 14, 13, 16, 12, 5, 20, 8, 19 };
    if (static_cast<unsigned>(race) >= 10u) return ENEMY_NORMAL;
    return static_cast<EnemyType>(kMortalEnemy[race]);
}

// The "time to promotion" draw — CONFIRMED against the .so by the unit differential
// (tools/emu/unit_stats.py, analysis/validation/UNIT_STATS.md, tasks/V6): native and rebuild agree
// on Game+0x14338 and on GetRandSeed(RAND_TILE) for every tested turn/difficulty/mode.
// GenerateStats @ 0x63098 rolls RandI((int)(C / s), (int)(300.0f / s), RAND_TILE) with s the SAME
// vsqrt.f32 result for both bounds (d=0 @ 0x635f8, d=1 @ 0x63550, d=2/3 @ 0x631c8):
//   d=0: C = 200.0, s = sqrt(T),  result + 5   (vldr 0x6361e/0x63626, adds r6,#5 @ 0x63650)
//   d=1: C = 200.0, s = sqrt(p)                (vldr 0x6357a -> b 0x6320a)
//   d=2,3: C = 150.0, s = sqrt(p)              (vldr 0x63206 falls into the shared block)
//   d>3: 1, no draw                            (movs r6,#1 @ 0x63686)
// shared tail LAB_0006320a @ 0x6320a does the two vdiv.f32, vcvt.s32.f32 (toward zero) and calls
// RandI @ 0x63228; the easy path calls RandI @ 0x63640. Literals 200.0 @ 0x63448,
// 150.0 @ 0x63430, 300.0 @ 0x63440 (RandI itself @ 0x67bf4).
// p >= 0 always (turn >= 0, scale factor clamped >= 1), so the only degenerate input is p == 0:
// 200/0 = +inf and ARM vcvt.s32.f32(+inf) saturates to 0x7fffffff, which RandI(lo=hi=INT_MAX)
// returns after one draw (native turn-0 value observed as 2147483647). x86 (int)(+inf) is
// 0x80000000, so the explicit branch below is required — it is not a workaround for a guess.
static int PromotionTimer(float lo, float p)
{
    const float s = std::sqrt(p);
    if (s <= 0.0f) {
        // p == 0: see above; the RandI call at GenerateStats LAB_0006320a still executes and
        // consumes one RAND_TILE draw (proved by the stream: first spawn after a skull lands on
        // draw t+5, not t+4). The rolled value is unused here beyond SetTimeToPromotion.
        RandI(0x7fffffff, 0x7fffffff, RAND_TILE);
        return 0x7fffffff;
    }
    return RandI(static_cast<int>(lo / s), static_cast<int>(300.0f / s), RAND_TILE);
}

// Tile::GenerateStats @ 0x63098 — attack/armour/health from turn count, XP level and difficulty tier,
// then the per-enemy-type multipliers. turn = Game+0xc, difficulty = Game+0x14328.
void Tile::GenerateStats(int turn, Difficulty difficulty)
{
    Game *g = game;
    const int X = g->xp + g->xpHundreds;                 // total XP = +0xc170 + +0xc210
    int  tier = static_cast<int>(difficulty);
    bool sprintOverride = false;                         // challenge variant 0
    int  L;                                              // level term

    if (g->challenge) {                                  // +0x14335
        if (g->mode == MODE_SPRINT) {                    // +0x14330 == 0
            L = std::abs(X) / 400;                       // level term 4x later
            sprintOverride = true;                       // stats overwritten after the tier switch
        } else {
            L = X / 100;
            if (g->mode == MODE_PRETZEL) tier = DIFF_NORMAL;   // +0x14330 == 1: tier switch skipped, Normal row
        }
    } else {
        L = X / 100;
    }

    int p = turn;          // scaled turn value used by tiers 1..3 (param_1 in the decomp)
    int timer = 1;         // time-to-promotion draw

    // p = T * max(1 + (T-500)/div, 1) * (L > 40 ? 1 + (L-40)/60 : 1)
    auto scale = [L](int &v, float div) {
        float m = (static_cast<float>(v) - 500.0f) / div + 1.0f;
        if (m < 1.0f) m = 1.0f;
        v = static_cast<int>(static_cast<float>(v) * m);
        if (L > 40)
            v = static_cast<int>(static_cast<float>(v) * ((static_cast<float>(L) - 40.0f) / 60.0f + 1.0f));
    };

    switch (tier) {
    case DIFF_EASY:                                       // p = T, no scaling
        attack  = std::max(1, RandI(turn / 80, turn / 70, RAND_TILE));
        defence = RandI(turn / 70, turn / 55, RAND_TILE);
        health  = std::max(3, RandI(turn / 25, turn / 20, RAND_TILE));
        timer   = PromotionTimer(200.0f, static_cast<float>(turn)) + 5;
        break;
    case DIFF_NORMAL:
        scale(p, 700.0f);
        attack  = std::max(1, RandI(p / 65, p / 55, RAND_TILE));
        defence = RandI(p / 50, p / 40, RAND_TILE);
        health  = std::max(4, RandI(p / 20, p / 15, RAND_TILE));
        timer   = PromotionTimer(200.0f, static_cast<float>(p));
        break;
    case DIFF_HARD:
        scale(p, 750.0f);
        attack  = std::max(1, RandI(p / 50, p / 40, RAND_TILE));
        defence = RandI(p / 40, p / 37, RAND_TILE);
        health  = std::max(4, RandI(p / 17, p / 12, RAND_TILE));
        timer   = PromotionTimer(150.0f, static_cast<float>(p));
        break;
    case DIFF_HARDER:
        scale(p, 500.0f);
        attack  = std::max(1, RandI(p / 40, p / 35, RAND_TILE));
        defence = RandI(p / 30, p / 35, RAND_TILE);        // decomp order (RandI swaps bounds)
        health  = std::max(4, RandI(p / 15, p / 10, RAND_TILE));
        timer   = PromotionTimer(150.0f, static_cast<float>(p));
        break;
    default:
        timer = 1;                                         // base stats left untouched
        break;
    }

    // Challenge variant 0 overwrites the three stats after the tier switch. CONFIRMED (tasks/V6):
    // the override block @ 0x63248..0x632a8 divides r5, i.e. the tier-scaled p (the raw turn only
    // for Easy, which does not scale) — ENEMY_STATS.md's "RandI(T/20, T/12)" is loose.
    if (sprintOverride) {
        attack  = std::max(1, RandI(p / 20, p / 12, RAND_TILE));
        defence = RandI(p / 10, p / 6, RAND_TILE);
        health  = std::max(4, RandI(p / 8, p / 4, RAND_TILE));
    }

    // Player flaws applied before the type multipliers (analysis/progression/PERKS_FLAWS.md).
    if (enemyType == ENEMY_NORMAL) {
        if (g->HaveFlaw(FLAW_SNEAKY)) health += 1;                       // flaw 3: normal enemies +1 HP
    } else {
        if (g->HaveFlaw(FLAW_ZEALOUS) && --timer < 1) timer = 1;        // flaw 8: promotion timer -1, min 1
        g->SetTimeToPromotion(timer);                                    // -> Game+0x14338
        if (defence == 0) defence = 1;
    }

    // Per-type multipliers, switch(this+0x25c). The decomp's fallthroughs (3->10 fuse block,
    // 0x10/0x1b -> 0x1b health block) are written out explicitly here.
    switch (enemyType) {
    case ENEMY_BOSS:        attack *= 3; defence *= 3; health *= 6; break;
    case ENEMY_SPIKY:       attack *= 2;                health *= 3; break;   // armour x1
    case ENEMY_GOLDEN:      attack  = 0; defence *= 6; health *= 6; countdown = 5; break;
    case ENEMY_INVISIBLE:   attack *= 2; defence *= 10; health  /= 2; break;
    case ENEMY_POISONOUS:              defence *= 2; health *= 4; break;      // attack x1
    case ENEMY_KAMIKAZE:               defence *= 2; health *= 6; countdown = 5; break;
    case ENEMY_ASSASSIN:    attack *= 3; defence *= 2; health *= 3; break;
    case ENEMY_DOPPELGANGER:attack *= 2; defence  = g->armour; health *= 4; break;  // = player armour (+0xc164)
    case ENEMY_UNSTABLE:    attack *= 4;                health *= 5; countdown = 5; break;  // armour x1
    case ENEMY_ARMOURED:    attack *= 2; defence *= 4; health *= 3; break;
    case ENEMY_DEVOURER:    attack *= 2; defence *= 2; health *= 4; countdown = 5; break;
    case ENEMY_MEAT_SHIELD: attack /= 2;                health *= 5; break;   // armour x1
    default:                attack *= 2; defence *= 2; health *= 4; break;     // default arm, then case 0 (no-op)
    case ENEMY_NORMAL:                                                        break;
    }

    if (enemyType != ENEMY_NORMAL && g->HaveFlaw(FLAW_NOISY))            // flaw 9: enemies +5% HP
        health = static_cast<int>(static_cast<float>(health) * 1.05f);    // DAT_0006344c = 1.05

    // Race mortal enemy = Invisible and an Invisible already on the board -> armour x2.
    // CONFIRMED (tasks/V6): the native test @ 0x63740..0x63748 is only
    // GetMortalEnemyType(...) == 5 && Game::GetEnemyCount(5) != 0 — it never tests this tile's own
    // type, and because type is already TILE_SKULL when GenerateStats runs, a self-spawned
    // Invisible counts itself and always gets the x2.
    if (g->ActiveClass().MortalEnemy() == ENEMY_INVISIBLE && g->GetEnemyCount(ENEMY_INVISIBLE) != 0)
        defence *= 2;

    UpdateCountdown(*this);
    maxHealth = health;                                                    // +0xb4 = +0xb0
}

// Tile::SetType @ 0x64ef8 — full reset of a cell to type t, then (for skulls) roll stats or promote.
// The atlas-column roll RandI(0,7,RAND_COSMETIC) (+0x23c) is consumed for stream-0 bookkeeping
// (V24: the original draws it here, sharing the stream with the trophy countdown @ 0x2ac40:255);
// A16b: the value is stored in faceFrame — the original keeps it at +0x23c and UpdateEnemyUV
// @0x64b04:24-31 uses it as the skull-face atlas column (enemyType 0 only). The white quad
// colour and the UV math are render-only and not rebuilt; neither are +0x89, +0x234 (glow),
// +0x1bc (overlay), +0x254/+0x255 (poison pulse).
void Tile::SetType(TileType t, int turn, Difficulty difficulty)
{
    faceFrame = RandI(0, 7, RAND_COSMETIC);   // +0x23c atlas column — stream-0 draw (0x64ef8:23,29)
    Rng0Trace("settype", 1);
    type           = t;                    // +0xa0
    enemyType      = ENEMY_NORMAL;         // +0x25c
    specialVariant = SPECIAL_TILE_NONE;    // +0xa4 = 3

    magicSword = arrow = manaPotion = acidPotion = explosivePotion = false;   // +0x240..+0x244
    frozen = flaming = flamePulse = false;                                    // +0x265, +0x266, +0x267
    broken = false;                                                           // +0x26c
    countdown = 0;                                                            // +0x260
    promoted = false;                                                         // +0x26d
    resurrectBlocked = false;                                                 // +0x26e

    if (type == TILE_SKULL) {
        poisonTurns = poison = poisonAddedThisTurn = 0;                       // +0x248/+0x24c/+0x250
        if (game->IsTimeToPromote())
            PromoteInternal(*this, turn, difficulty, false, ENEMY_NORMAL);    // Promote(..., false)
        else {
            GenerateStats(turn, difficulty);
            UpdateValues(*this);
        }
    }
}

// Tile::Promote @ 0x64e3c body. The original signature is Promote(int turn, int difficulty, bool
// flag) and it has NO forced-type parameter: it rolls RandI(1,0x20) (@:7-8), may override with
// MortalEnemy() when RandI(1,200) <= (perk 8 ? 5 : 10) (@:20-34), then GenerateStats(turn,
// difficulty) (@:35) and `this[0x26d] = flag` (@:44; true from the public Promote, false from
// SetType). The `forced` argument below is rebuild-side; its only caller passes ENEMY_NORMAL
// (skills.cpp:590), so the forced branch is dead.
static void PromoteInternal(Tile &t, int turn, Difficulty difficulty, bool flag, EnemyType forced)
{
    Game *g = t.game;
    CharacterClass &cls = g->ActiveClass();
    EnemyType e = (forced != ENEMY_NORMAL) ? forced
                                           : static_cast<EnemyType>(RandI(1, 32, RAND_TILE));   // +0x25c
    if (cls.klass != CLASS_ADVENTURER) {                                   // class+0x4 != 0 ("no class")
        const int chance = cls.HavePerk(PERK_INTIMIDATING) ? 5 : 10;       // perk 8 (Intimidating)
        if (RandI(1, 200, RAND_TILE) <= chance) e = cls.MortalEnemy();
    }
    t.enemyType = e;
    t.GenerateStats(turn, difficulty);
    UpdateEnemyUV(t);
    UpdateValues(t);
    if (g->GetSpecialEnemyCount() > 2)
        g->fx().QueueAchievement(ACH_GETTING_CROWDED, 100.0f);             // 0x29, progress 100.0
    t.promoted = flag;
}

// Tile::Promote @ 0x64e3c — public entry point (see PromoteInternal for the reconstruction).
void Tile::Promote(EnemyType to) { PromoteInternal(*this, game->turn, game->difficulty, true, to); }

// Tile::GetPoison @ 0x62b0c — the EndOfTurn poison tick. When +0x248 (poisonTurns) != 0 it
// rotates the three poison counters — +0x248 <- old +0x24c, +0x24c <- +0x250, +0x250 <- 0
// (decomp @:10-16) — and the caller uses the RETURN VALUE as this turn's tick damage. Ghidra
// shows the function as void, but the return left in r0 is the PRE-rotation +0x248 (the load
// that guards the early-out survives in r0; measured native seed 2 m51-54 with
// tools/emu/k5_poison_probe.py: (0,0,0)->r0=0, (2,2,2)->r0=2, (2,2,0)->r0=2, (2,0,0)->r0=2,
// (4,4,2)->r0=4, (4,2,0)->r0=4 — always old +0x248, never old +0x24c). The +0x254/+0x258/+0x255
// poison-pulse UI bytes are not modelled.
int Tile::GetPoison()
{
    if (poisonTurns == 0) return 0;               // +0x248 == 0 -> early-out, no rotation
    const int tick = poisonTurns;                 // returned to the caller (EndOfTurn @0x3289c:211)
    poisonTurns = poison;                         // +0x248 <- +0x24c
    poison      = poisonAddedThisTurn;            // +0x24c <- +0x250
    poisonAddedThisTurn = 0;                      // +0x250 <- 0
    return tick;
}

// Tile::ActivateAbility @ 0x64c78 — once-per-turn ability dispatch. Game::EndOfTurn @ 0x3289c clears
// abilityFired (+0x264) and then calls this for every skull with enemyType != 0, so each ability fires
// at most once per turn. Enemy types with no case (0,1,2,5,9,12,15,20,21,27,29,30) have no ability.
void Tile::ActivateAbility()
{
    if (abilityFired) return;
    abilityFired = true;                       // +0x264 = 1

    switch (enemyType) {
    case ENEMY_GOLDEN:                                                     // 3
        countdown -= 1; UpdateCountdown(*this);
        if (countdown == 0) game->RunAwayEnemy(*this);
        break;
    case ENEMY_HEALER:      game->HealOrBless(*this); break;               // 4
    case ENEMY_ACIDIC:      game->MakeAcidPotion(*this); break;            // 6
    case ENEMY_FLAMING:     game->IgniteTiles(*this); break;               // 7
    case ENEMY_FROZEN:      game->FreezeTiles(*this); break;               // 8
    case ENEMY_KAMIKAZE:                                                   // 10
        countdown -= 1; UpdateCountdown(*this);
        if (countdown == 0) game->ExplodeEnemy(*this);
        break;
    case ENEMY_OVERSEER:    game->ConvertSwordToSkull(*this); break;       // 11
    case ENEMY_THIEF:       game->StealGold(*this); break;                 // 13
    case ENEMY_CHAOTIC:     game->JumpEnemy(*this); break;                 // 14
    case ENEMY_ASSASSIN:    game->StabAttack(*this); break;                // 16
    case ENEMY_DOPPELGANGER:game->MimicDefence(*this); break;              // 17
    case ENEMY_ALCHEMIST:   game->ConvertRandomTiles(*this); break;        // 18
    case ENEMY_UNSTABLE:                                                    // 19
        countdown -= 1; UpdateCountdown(*this);
        if (countdown == 0) game->TeleportEnemy();
        break;
    case ENEMY_TRAMPLING:   game->BreakRandomShieldSword(*this); break;    // 22
    case ENEMY_GREASY:      game->LowerSkulls(*this); break;               // 23
    case ENEMY_ALPHA:       game->AttractSkulls(*this); break;             // 24
    case ENEMY_MAGNETIC:    game->AttractShieldsSwords(*this); break;      // 25
    case ENEMY_BRUTE:       game->ThrowSkull(*this); break;                // 26
    case ENEMY_DEVOURER: {                                                // 28: recurring 5-turn fuse
        const int c = countdown - 1; countdown = c;
        if (c == 0) { game->DestroyUpgrade(*this); countdown = 5; }
        UpdateCountdown(*this);
        break;
    }
    case ENEMY_FLEEING:     game->FleeUp(*this); break;                    // 31
    case ENEMY_LICH:                                                        // 32 — Phylactery -> respawn
        if (countdown != 0) {
            if (!resurrectBlocked) {                                         // +0x26e == 0
                countdown = 0;
                GenerateStats(game->turn, game->difficulty);                // fresh stats at current T/d
                UpdateEnemyUV(*this);
                UpdateValues(*this);
                game->Resurrect(*this);
            } else {
                resurrectBlocked = false;                                    // clear the 1-turn window
            }
        }
        break;
    default: break;
    }
}

// Game::GetSpecialEnemyCount @ 0x24704 — skulls with an enemy type != 0.
int Game::GetSpecialEnemyCount() const
{
    int n = 0;
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row)
            if (board[col][row].type == TILE_SKULL && board[col][row].enemyType != ENEMY_NORMAL) ++n;
    return n;
}

// Game::SetTimeToPromotion @ 0x2985c — store the next promotion countdown (+0x14338) unless promotion
// is disabled; sprint-variant-0 challenges pin it to 200 instead. Sound 0xe (SOUND_SKULL).
void Game::SetTimeToPromotion(int t)
{
    if (!promotionEnabled) return;                                          // +0x1433c
    if (!challenge || mode != MODE_SPRINT) promoteCountdown = t;
    else promoteCountdown = 200;
    fx().QueueSound(SOUND_SKULL);
}

// Tile::GetAttack @ 0x62934 — a poisoned enemy (+0x248 > 0) whose ACTIVE class's RACE (+8, read
// by the native as Game+0x2800 — ClassScreen::SelectTab @0x39ba8:82 passes that exact field to
// CharacterClass::GetRaceName) is 1 (elf) hits for half (asreq r0,#1). Cited:
// GetAttack@00062934.c ARM 0x6293a-0x62964. K13: the rebuild read class LEVEL here, so the
// level-1 human Adventurer halved poisoned enemies the native leaves unhalved (seed 2 m51:
// vengeful (3,5) GetAttack 3 vs native 7, incoming 8 vs 12, hp 30 vs 26).
int Tile::GetAttack() const
{
    int a = attack;                                   // +0xa8
    if (a > 1 && poisonTurns > 0 && game != nullptr &&
        game->ActiveClass().race == RACE_ELF)         // +0x2800 == 1
        a >>= 1;
    return a;
}

} // namespace dr
