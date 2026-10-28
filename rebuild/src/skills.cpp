// Skill system: Skill::CanActivate / Skill::Activate plus the Game:: effect functions they
// dispatch to. Sources: analysis/progression/SKILLS.md (dispatch table) and the decomp of
// Skill::CanActivate @ 0x60100/0x60204, Skill::Activate @ 0x60210, and each effect function
// (address cited per function). Rendering-only side effects (Tile UV/colour helpers, flare
// RGBA colours, the +0xc150/+0xc154 screen-shake pair, Tile::UpdateText) are not modelled;
// gameplay-visible flags are set directly and the original helper is cited in a comment.
#include "dr/game.h"
#include "dr/skill.h"
#include "dr/rng.h"
#include <utility>                                  // std::move (RemoveSkill array shift)

namespace dr {
namespace {

// Platform::AddFlare only takes a colour-kind int (chain.cpp convention: 0 red, 1 yellow); the
// native AddFlare(game, kind, x, y, &rgba) takes an explicit RGBA quad (ConvertTiles @0x2a440:34-40),
// so the kind indices below are rebuild-side presentation only — comments give the original bytes.
constexpr int FLARE_SKY      = 2;   // 100,100,255,255  magic sword / chain tile
constexpr int FLARE_ICE      = 3;   //  64,230,255,255  Freeze
constexpr int FLARE_MANA     = 4;   //  64,100,255,255  mana potions
constexpr int FLARE_ORANGE   = 5;   // 255,150,0,255    explosive potions (QA_GAME_SKETCH 0x29df4)
constexpr int FLARE_EARTH    = 6;   // 115,60,20,255    Earthquake / Shock / Awe
constexpr int FLARE_WHITE    = 7;   // 255,255,255,255  arrows / ConvertTiles / Haste / Banish
constexpr int FLARE_TELEPORT = 8;   //   0,100,255,255  Teleport (QA_GAME_SKETCH: blue, not white)
constexpr int FLARE_GREEN    = 9;   //   0,255,0,255    Exorcise (demoted enemy)
constexpr int FLARE_PROMOTE  = 10;  // 240,150,10,255   Big Game Hunter / Explosive Armour

// MatchTile's decomp argument: 5 = pay the tile's own type, 7 = clear with no reward (Banish).
// chain.cpp keeps file-local names MATCH_USE_OWN / MATCH_NO_REWARD; re-declared here.
constexpr TileType MATCH_USE_OWN   = static_cast<TileType>(5);
constexpr TileType MATCH_NO_REWARD = static_cast<TileType>(7);

// Post-gravity epilogue shared by the collect skills: clear the per-cell chain flag +0x8a on
// every tile (the original inlines this 36-byte clear loop).
void ClearInChainOnAll(Game &g)
{
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row)
            g.board[col][row].inChain = false;      // +0x8a = 0
}

// Tile::SetFlameVisibility @ 0x62de8 (on path) — state change only (the Quad UV/alpha block at
// :26-44 is rendering). Same helper as the file-local SetFlame in enemy_abilities.cpp, kept here
// so skills.cpp does not depend on that TU: +0x268 damage (+= while already burning), +0x267 =
// param_3, +0x266 = 1, +0x265 = 0 (an existing flame thaws the tile).
void SetFlameOn(Tile &t, int damage, bool pulse)
{
    t.flameDamage = t.flaming ? t.flameDamage + damage : damage;   // +0x268
    t.flamePulse = pulse;                                          // +0x267 = param_3
    t.flaming = true;                                              // +0x266 = 1
    t.frozen = false;                                              // +0x265 = 0
}

// Skill::CanActivate(int) @ 0x60100 — precondition per SkillType (SKILLS.md table, exact
// decomp switch). File-local because skill.h only declares the no-arg overload @ 0x60204.
bool CanActivateType(const Skill &s, int type)
{
    const Game &g = *s.game;
    switch (type) {
    case 0:  case 1:  case 4:  case 34:                       // Steal/Freeze/Counterattack/Berserk
        return g.GetTileCount(TILE_SKULL) >= 1;
    case 2:  case 8:  case 9:  case 29:                       // Heal/Explosive Potion/Skill Elixir/Mana
        return g.GetNormalPotionCount() > 0;
    case 3:                                                   // Repair
        return g.GetTileCount(TILE_SHIELD) > 0;
    case 5:  case 14: case 23:                                // Shatter/Disarm/Big Game Hunter
        return g.GetTileCount(TILE_SKULL) - g.GetSpecialEnemyCount() > 0;   // normal enemies only
    case 6:  case 13:                                         // Golden Touch/Scavenge
        return g.GetTileCount(TILE_SWORD) > 0;
    case 20: case 21:                                         // Exorcise/Banish
        return g.GetSpecialEnemyCount() > 0;
    case 25: case 31:                                         // Magic Sword/Volley
        return g.GetNormalSwordCount() > 0;
    case 27:                                                  // Explosive Armour
        return g.GetTileCount(TILE_SKULL) != 0 && g.armour > 0;              // +0xc164
    case 12: case 15: case 32:                                // Treasure/Dazzle/Masochism
        return g.GetTileCount(TILE_COIN) > 0;
    case 36:                                                  // Greed: a normal enemy AND a coin
        return g.GetTileCount(TILE_SKULL) - g.GetSpecialEnemyCount() > 0 &&
               g.GetTileCount(TILE_COIN) > 0;
    default:                                                  // everything else is always usable
        return true;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Skill::CanActivate() @ 0x60204 — dispatch on this+0x84 (SkillType).
// ---------------------------------------------------------------------------
bool Skill::CanActivate() const
{
    return CanActivateType(*this, type);                      // +0x84
}

// ---------------------------------------------------------------------------
// Skill::Activate() @ 0x60210 — the big switch on this+0x84 (SKILLS.md rows 0..41).
// "Turn" skills (those that only set Game flags) mark active (+0x80) and snapshot the
// +0x14d94 marker byte into their carry field; effect skills call a Game:: function.
// ---------------------------------------------------------------------------
void Skill::Activate()
{
    Game &g = *game;                                          // +0x7c
    switch (type) {
    case 0:                                                   // Steal
        g.fx().QueueSound(SOUND_ABILITY);                     // 0x13
        g.stealTokenMode = true;                              // +0xc383
        g.carrySteal = g.turnSkillMarker;                     // +0xc384 = +0x14d94
        active = true;                                        // +0x80
        break;
    case 1:                                                   // Freeze
        g.fx().QueueSound(SOUND_FREEZE);                      // 0x14
        g.FreezeSkulls();                                     // @ 0x29eb0
        break;
    case 2:                                                   // Heal
        g.fx().QueueSound(SOUND_ABILITY);                     // 0x13
        g.GrabTiles(TILE_POTION, TILE_POTION);                // GrabTiles(0,0) @ 0x30e40
        break;
    case 3:                                                   // Repair
        g.fx().QueueSound(SOUND_ABILITY);
        g.GrabTiles(TILE_SHIELD, TILE_SHIELD);                // GrabTiles(3,3)
        break;
    case 4:                                                   // Counterattack
        g.fx().QueueSound(SOUND_ABILITY);
        g.counterAttackPending = true;                        // +0xc382 (board.cpp extras)
        active = true;
        break;
    case 5:                                                   // Shatter
        g.fx().QueueSound(SOUND_SHATTER);                     // 0x16
        g.ShatterDefence();                                   // @ 0x28554
        break;
    case 6:                                                   // Golden Touch
        g.fx().QueueSound(SOUND_ABILITY);
        g.ConvertTiles(TILE_SWORD, TILE_COIN);                // ConvertTiles(4,2) @ 0x2a440
        break;
    case 7:                                                   // Teleport
        g.fx().QueueSound(SOUND_TELEPORT);                    // 0x15
        g.Teleport();                                         // @ 0x2ffb4
        break;
    case 8:                                                   // Explosive Potion
        g.fx().QueueSound(SOUND_EXPLOSIVE);                   // 0x18
        g.CreateExplosivePotions();                           // @ 0x29df4
        break;
    case 9:                                                   // Skill Elixir
        g.fx().QueueSound(SOUND_ABILITY);
        g.GrabTiles(TILE_POTION, TILE_SKULL);                 // GrabTiles(0,1): potions as XP
        break;
    case 10:                                                  // Enchant
        g.fx().QueueSound(SOUND_ABILITY);
        g.Upgrade();                                          // @ 0x251d4
        break;
    case 11:                                                  // Slash
        g.fx().QueueSound(SOUND_SLASH);                       // 0x19
        g.CollectRandomRow();                                 // @ 0x30c8c (skill_board_effects.cpp)
        break;
    case 12:                                                  // Treasure
        g.fx().QueueSound(SOUND_ABILITY);
        g.GrabTiles(TILE_COIN, TILE_COIN);                    // GrabTiles(2,2)
        break;
    case 13:                                                  // Scavenge
        g.fx().QueueSound(SOUND_ABILITY);
        g.ConvertTiles(TILE_SWORD, TILE_SHIELD);              // ConvertTiles(4,3)
        break;
    case 14:                                                  // Disarm
        g.fx().QueueSound(SOUND_ABILITY);
        g.ConvertTiles(TILE_SKULL, TILE_SWORD);               // ConvertTiles(1,4)
        break;
    case 15:                                                  // Dazzle
        g.fx().QueueSound(SOUND_ABILITY);
        g.ConvertTiles(TILE_COIN, TILE_SWORD);                // ConvertTiles(2,4)
        break;
    case 16:                                                  // Boost Damage
        g.fx().QueueSound(SOUND_BOOST);                       // 0x17
        g.doubleDamage = true;                                // +0xc234
        g.carryDoubleDamage = g.turnSkillMarker;              // +0xc238
        active = true;
        break;
    case 17:                                                  // Boost Gold
        g.fx().QueueSound(SOUND_BOOST);
        g.doubleCoins = true;                                 // +0xc237
        g.carryDoubleGold = g.turnSkillMarker;                // +0xc23b
        active = true;
        break;
    case 18:                                                  // Boost Health
        g.fx().QueueSound(SOUND_BOOST);
        g.doublePotions = true;                               // +0xc236
        g.carryDoubleHealth = g.turnSkillMarker;              // +0xc23a
        active = true;
        break;
    case 19:                                                  // Boost Armour
        g.fx().QueueSound(SOUND_BOOST);
        g.doubleShields = true;                               // +0xc235
        g.carryDoubleArmour = g.turnSkillMarker;              // +0xc239
        active = true;
        break;
    case 20:                                                  // Exorcise
        g.fx().QueueSound(SOUND_ABILITY);
        g.DemoteSpecialEnemy();                               // @ 0x2a34c
        break;
    case 21:                                                  // Banish
        g.fx().QueueSound(SOUND_ATTACK_MISS);                 // 0x1a
        g.KillSpecialEnemy();                                 // @ 0x30178
        break;
    case 22:                                                  // Trap
        g.fx().QueueSound(SOUND_ATTACK_HIT);                  // 0x1b
        g.oneHitSkulls = true;                                // +0x1433d (board.cpp extras)
        g.carryOneHitSkulls = g.turnSkillMarker;              // +0x1433f
        active = true;
        break;
    case 23:                                                  // Big Game Hunter
        g.fx().QueueSound(SOUND_SKULL);                       // 0xe
        g.PromoteNormalEnemy();                               // @ 0x2d454
        break;
    case 24:                                                  // Treasure Chamber
        g.fx().QueueSound(SOUND_DOOR);                        // 0x22
        g.forceCoinMode = true;                               // +0x1433e (board.cpp extras)
        g.carryForceCoin = g.turnSkillMarker;                 // +0x14340
        active = true;
        break;
    case 25:                                                  // Magic Sword
        g.fx().QueueSound(SOUND_ABILITY);
        g.CreateMagicSword();                                 // @ 0x2a1cc
        break;
    case 26:                                                  // Earthquake
        g.fx().QueueSound(SOUND_QUAKE);                       // 0x25
        g.Earthquake();                                       // @ 0x2a2ac
        break;
    case 27:                                                  // Explosive Armour
        g.fx().QueueSound(SOUND_EXPLOSION);                   // 0x1c
        g.ExplodeArmour();                                    // @ 0x31050
        break;
    case 28:                                                  // Fireball
        g.fx().QueueSound(SOUND_EXPLOSION);
        g.CollectRandomBlock();                               // @ 0x30554 (skill_board_effects.cpp)
        break;
    case 29:                                                  // Mana Potion
        g.fx().QueueSound(SOUND_EXPLOSIVE);                   // 0x18
        g.CreateManaPotions();                                // @ 0x29f74
        break;
    case 30:                                                  // Hack
        g.fx().QueueSound(SOUND_ATTACK_HIT);                  // 0x1b
        g.ConvertColumnToSwords();                            // @ 0x30b88 (skill_board_effects.cpp)
        break;
    case 31:                                                  // Volley
        g.fx().QueueSound(SOUND_ATTACK_MISS);                 // 0x1a
        g.CreateArrows();                                     // @ 0x2a108
        break;
    case 32:                                                  // Masochism
        g.fx().QueueSound(SOUND_SKULL);                       // 0xe
        g.ConvertTiles(TILE_COIN, TILE_SKULL);                // ConvertTiles(2,1)
        break;
    case 33: {                                                // Wish (single use)
        if (g.GetTileCount(TILE_SKULL) - g.GetSpecialEnemyCount() > 19)
            g.fx().QueueAchievement(ACH_WHEN_YOU_WISH, 100.0f);   // 0x40 @ 0x42c80000
        g.fx().QueueSound(SOUND_ABILITY);
        g.CollectBoard();                                     // @ 0x30a2c (skill_board_effects.cpp)
        g.RemoveSkill(this);                                  // @ 0x24eec
        break;
    }
    case 34:                                                  // Berserk
        g.fx().QueueSound(SOUND_SKULL);                       // 0xe
        g.berserkActive = true;                               // +0x15cef
        g.carryBerserk = g.turnSkillMarker;                   // +0x15cf0
        active = true;
        break;
    case 35:                                                  // Rune
        g.fx().QueueSound(SOUND_ABILITY);
        g.runeActive = true;                                  // +0x15cd9
        // Arrow::SetColour(Game+0x3668, 0x50, 0x82, 0xff) — chain-cursor tint, rendering only
        g.carryRune = g.turnSkillMarker;                      // +0x15cda
        active = true;
        break;
    case 36:                                                  // Greed
        g.fx().QueueSound(SOUND_COIN);                        // 0x02, queued 3x
        g.fx().QueueSound(SOUND_COIN);
        g.fx().QueueSound(SOUND_COIN);
        g.KillSkullsNextToCoins();                            // @ 0x306fc (skill_board_effects.cpp)
        break;
    case 37:                                                  // Last Stand
        g.fx().QueueSound(SOUND_ABILITY);
        g.reviveUsed = true;                                  // +0x15cec (resources.cpp extras)
        g.carryRevive = g.turnSkillMarker;                    // +0x15ced
        active = true;
        break;
    case 38:                                                  // Charge
        g.fx().QueueSound(SOUND_ATTACK_HIT);                  // 0x1b
        g.CollectEdgeTiles();                                 // @ 0x308c8 (skill_board_effects.cpp)
        break;
    case 39:                                                  // Haste
        g.fx().QueueSound(SOUND_BOOST);                       // 0x17
        g.CreateChain();                                      // @ 0x2a038
        break;
    case 40:                                                  // Shock
        g.fx().QueueSound(SOUND_EXPLOSION);                   // 0x1c
        g.Shock();                                            // @ 0x30404
        break;
    case 41:                                                  // Awe
        g.fx().QueueSound(SOUND_EXPLOSION);
        g.Awe();                                              // @ 0x302bc
        break;
    default:
        break;
    }
}

// Game::ActivateSkill — CONFIRMED there is no native body of this name (no match for ActivateSkill
// in analysis/decomp/INDEX.tsv); the Java/SWIG side taps a skill slot and the native entry runs
// this check-then-Activate sequence exactly as Interface::ProcessInput @ 0x49738:84-97 does on a
// skill-button release: CanActivate -> Activate -> charge reset -> Dragonkin FireBreath -> UpdateSkills.
void Game::ActivateSkill(int slot)
{
    if (slot < 0 || slot >= static_cast<int>(skills.size())) return;
    Skill &s = *skills[slot];                                 // array at +0xc358
    if (!s.CanActivate()) return;                             // ProcessInput @0x49738:86-88
    s.Activate();                                             // ProcessInput @0x49738:90
    // ProcessInput @0x49738:91-92: +0x90 = +0x8c & ((int)+0x8c >> 31) — i.e. charge = 0 while
    // maxCharge >= 0 (passive skills keep -1); the skill recharges from 0 (EndOfTurn @0x3289c).
    s.charge = (s.maxCharge < 0) ? s.maxCharge : 0;
    if (Race() == RACE_DRAGONKIN)                            // ProcessInput @0x49738:93-94:
        FireBreath();                                        //   class(+0x15cd4)->race(+0x14db4)==8
    fx().OnSkillsChanged();                                  // Interface::UpdateSkills @0x49544
}

// ---------------------------------------------------------------------------
// Game::FireBreath @ 0x29cfc — Dragonkin (race 8) racial, called by
// Interface::ProcessInput @ 0x49738:93-96 after EVERY successful skill activation.
// One random NORMAL enemy catches fire with the player's base damage; then the
// screen-shake pair +0xc150/+0xc154 = (0.0f, 1.0f) — animation only (see skills.cpp header).
// ---------------------------------------------------------------------------
void Game::FireBreath()
{
    // FireBreath @0x29cfc:17-19 — index over the normal enemies (skulls minus specials), stream 5.
    int idx = RandI(0, GetTileCount(TILE_SKULL) - 1 - GetSpecialEnemyCount(), RAND_OTHER);
    if (idx >= 0) {                                          // @0x29cfc:20
        for (int col = 0; col < BOARD_SIZE && idx >= 0; ++col)          // @:21-47 column stride +0x270
            for (int row = 0; row < BOARD_SIZE; ++row) {                //           row stride +0xea0
                Tile &t = board[col][row];
                if (t.type != TILE_SKULL || t.enemyType != ENEMY_NORMAL) continue;   // @:26 +0xa0/+0x25c
                if (idx != 0) { --idx; continue; }                      // @:41
                fx().AddFlare(&t, FLARE_PROMOTE);                       // @:33 AddFlare(1, x, y,
                                                                       //   f0,96,0a,ff = 240,150,10,255)
                SetFlameOn(t, baseDamage, true);                        // @:35-37 SetFlameVisibility(
                                                                       //   true, +0xc180, true)
                idx = -1;                                              // @:38 break out of both loops
            }
    }
    // @0x29cfc:49-50 writes +0xc150 = 0.0f / +0xc154 = 1.0f (screen-shake floats, not modelled).
}

// ---------------------------------------------------------------------------
// Game::GetNormalSwordCount @ 0x2484c — plain swords (type 4, +0x240 magic / +0x241 arrow clear).
// ---------------------------------------------------------------------------
int Game::GetNormalSwordCount() const
{
    int n = 0;
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            const Tile &t = board[col][row];
            if (t.type == TILE_SWORD && !t.magicSword && !t.arrow) ++n;
        }
    return n;
}

// ---------------------------------------------------------------------------
// Game::GrabTiles @ 0x30e40 — collect every tile of `fromType` (potions: plain only),
// matched `as` `toType`, then gravity. Skills 2/3/9/12.
// ---------------------------------------------------------------------------
void Game::GrabTiles(int fromType, int toType)
{
    for (int i = 0; i < TILE_TYPE_COUNT; ++i) clearedThisChain[i] = 0;   // +0xc36c..+0xc37c
    explosionSoundPlayed = false;                           // +0x142f4 once-per-chain blast flag
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            Tile &t = board[col][row];
            if (t.type != fromType) continue;
            // potions: only plain ones — explosive/mana/acid flags (+0x244/242/243) exclude
            if (fromType == TILE_POTION &&
                (t.explosivePotion || t.manaPotion || t.acidPotion)) continue;
            MatchTile(t, static_cast<TileType>(toType));    // @ 0x2ac40 (chain.cpp)
        }
    MoveMatchedTiles();                                     // @ 0x2fd7c gravity + refill
    if (!turnSkillMarker)                                   // +0x14d94 == 0
        for (int col = 0; col < BOARD_SIZE; ++col)
            for (int row = 0; row < BOARD_SIZE; ++row)
                board[col][row].inChain = false;            // clear +0x8a on all 36 tiles
}

// ---------------------------------------------------------------------------
// Game::ConvertTiles @ 0x2a440 — re-type every matching tile (skills 6/13/14/15/32).
// Skulls: only NORMAL enemies convert (skull specials +0x25c != 0 are kept).
// Swords: arrows (+0x241) are kept.
// Iteration order CONFIRMED row-major: the native outer loop (iVar4) walks rows with tile
// stride +0xea0 and the inner loop (iVar3) walks cols with stride +0x270 (SetType addr
// `this + iVar3*0x270 + iVar4*0xea0 + 0x5fcc`, ConvertTiles @0x2a440:11-13). For Masochism
// (coin->skull) the FIRST coin converted triggers IsTimeToPromote->Promote (SetType @0x64ef8),
// so order decides which coin becomes the special enemy (K10 s10).
// ---------------------------------------------------------------------------
void Game::ConvertTiles(int fromType, int toType)
{
    for (int row = 0; row < BOARD_SIZE; ++row)
        for (int col = 0; col < BOARD_SIZE; ++col) {
            Tile &t = board[col][row];
            if (fromType == TILE_SKULL && t.enemyType != ENEMY_NORMAL) continue;
            if (t.type != fromType) continue;
            if (fromType == TILE_SWORD && t.arrow) continue;
            t.SetType(static_cast<TileType>(toType), turn, difficulty);   // @ 0x64ef8 (tile.cpp)
            fx().AddFlare(&t, FLARE_WHITE);                 // AddFlare(1, cell, ff,ff,ff,ff)
        }
    // +0xc150/+0xc154 = screen-shake floats, animation only (not modelled)
    if (toType == TILE_SKULL && fromType == TILE_COIN)
        coinsToSkullsFlag = true;                           // +0x15e18: Masochism = skill 0x20,
                                                            //   Skill::Activate @0x60210:165 does
                                                            //   ConvertTiles(2,1) -> ConvertTiles @0x2a440:47-49
}

// ---------------------------------------------------------------------------
// Game::FreezeSkulls @ 0x29eb0 — Freeze (skill 1): freeze every enemy for the turn.
// ---------------------------------------------------------------------------
void Game::FreezeSkulls()
{
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            Tile &t = board[col][row];
            if (t.type != TILE_SKULL) continue;
            fx().AddFlare(&t, FLARE_ICE);                   // 64,e6,ff,ff
            t.frozen = true;                                // Tile::SetFreezeVisibility(true) @ 0x62e94
        }                                                   //   sets +0x1bc overlay + +0x265
}

// ---------------------------------------------------------------------------
// Game::ShatterDefence @ 0x28554 — Shatter (skill 5): normal enemies drop to 1 attack
// and 0 defence.
// ---------------------------------------------------------------------------
void Game::ShatterDefence()
{
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            Tile &t = board[col][row];
            if (t.type != TILE_SKULL || t.enemyType != ENEMY_NORMAL) continue;   // +0xa0/+0x25c
            if (t.defence > 0) {                            // +0xac
                fx().AddParticles(&t, 1);                   // AddParticles(game, tile, 1): r2 = r7
                                                            //   (the skull type) at ShatterDefence
                                                            //   @0x1859c-0x185b0 (was GUESS 0 — the
                                                            //   kind changes the stream-0 draw count)
                t.defence = 0;
                // Tile::UpdateText(false,true,false) @ 0x63938 — rendering
            }
            if (t.attack > 1) {                             // +0xa8
                fx().AddParticles(&t, 3);                   // `movs r2, #3` at ShatterDefence @0x185d0
                t.attack = 1;
                // Tile::UpdateText(true,false,false) — rendering
            }
        }
}

// ---------------------------------------------------------------------------
// Game::CreateExplosivePotions @ 0x29df4 — Explosive Potion (skill 8).
// ---------------------------------------------------------------------------
void Game::CreateExplosivePotions()
{
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            Tile &t = board[col][row];
            if (t.type != TILE_POTION) continue;
            if (t.explosivePotion || t.manaPotion || t.acidPotion) continue;
            fx().AddFlare(&t, FLARE_ORANGE);                // ff,96,00,ff (QA_GAME_SKETCH)
            t.explosivePotion = true;                       // Tile::MakeExplosivePotion @ 0x6438c
        }                                                   //   (+0x244; UV change is rendering)
}

// ---------------------------------------------------------------------------
// Game::CreateManaPotions @ 0x29f74 — Mana Potion (skill 29).
// ---------------------------------------------------------------------------
void Game::CreateManaPotions()
{
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            Tile &t = board[col][row];
            if (t.type != TILE_POTION) continue;
            if (t.explosivePotion || t.manaPotion || t.acidPotion) continue;
            fx().AddFlare(&t, FLARE_MANA);                  // 64,64,ff,ff
            t.manaPotion = true;                            // Tile::MakeManaPotion @ 0x642dc (+0x242)
        }
}

// ---------------------------------------------------------------------------
// Game::CreateArrows @ 0x2a108 — Volley (skill 31): every plain sword becomes an arrow.
// ---------------------------------------------------------------------------
void Game::CreateArrows()
{
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            Tile &t = board[col][row];
            if (t.type != TILE_SWORD || t.magicSword || t.arrow) continue;
            t.arrow = true;                                 // Tile::MakeArrow @ 0x64294 (+0x241)
            fx().AddFlare(&t, FLARE_WHITE);                 // ff,ff,ff,ff
        }
}

// ---------------------------------------------------------------------------
// Game::CreateMagicSword @ 0x2a1cc — Magic Sword (skill 25): one random plain sword
// becomes a x5-damage magic sword.
// ---------------------------------------------------------------------------
void Game::CreateMagicSword()
{
    int n = RandI(0, GetNormalSwordCount() - 1, RAND_OTHER);   // RandI(0, count-1, 5)
    if (n < 0) return;
    for (int col = 0; col < BOARD_SIZE && n >= 0; ++col)
        for (int row = 0; row < BOARD_SIZE && n >= 0; ++row) {
            Tile &t = board[col][row];
            if (t.type != TILE_SWORD || t.magicSword || t.arrow) continue;
            if (n != 0) { --n; continue; }
            t.magicSword = true;                            // Tile::MakeMagicSword @ 0x62db8 (+0x240)
            fx().AddFlare(&t, FLARE_SKY);                   // 100,100,255,255
            n = -1;                                         // orig breaks out after the hit
        }
}

// ---------------------------------------------------------------------------
// Game::CreateChain @ 0x2a038 — Haste (skill 39): one random tile becomes a chain tile, retrying
// up to 10 RandI(0,5,5) picks while the pick is a special enemy.
// ---------------------------------------------------------------------------
void Game::CreateChain()
{
    for (int tries = 0; tries < 10; ++tries) {
        int col = RandI(0, 5, RAND_OTHER);                 // @0x2a038:19-20 (2 draws per try)
        int row = RandI(0, 5, RAND_OTHER);
        Tile &t = board[col][row];
        // @0x2a038:22 — accept only when NOT (type == 1 && enemyType != 0):
        //   +0x606c = tile+0xa0 (type), +0x6228 = tile+0x25c (enemyType). The old comment read
        //   these as "tile+0x70 && tile+0x22c glow" — a mis-computed offset (V26); the rejected
        //   tiles are special enemies, which the rebuild can model exactly.
        if (t.type == TILE_SKULL && t.enemyType != ENEMY_NORMAL) continue;
        t.SetType(TILE_SPECIAL, 0, static_cast<Difficulty>(0));   // Tile::SetSpecialType(0) @ 0x65004
        t.specialVariant = SPECIAL_TILE_CHAIN;              //   SetType(6,0,0) then +0xa4 = 0
        fx().AddFlare(&t, FLARE_SKY);                       // 100,100,255,255
        break;
    }
    // @0x2a038:24-30 gives up after 10 tries without placing a tile; both paths then write the
    // screen-shake pair +0xc150/+0xc154 (animation only, not modelled).
}

// ---------------------------------------------------------------------------
// Game::Earthquake @ 0x2a2ac — Earthquake (skill 26): every cell swaps with a random cell.
// ---------------------------------------------------------------------------
void Game::Earthquake()
{
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            int c2 = RandI(0, 5, RAND_OTHER);
            int r2 = RandI(0, 5, RAND_OTHER);
            SwapTiles(col, row, c2, r2);                    // @ 0x28b14 (board.cpp)
            fx().AddFlare(&board[col][row], FLARE_EARTH);   // 73,3c,14,ff at the cell centre
        }
}

// ---------------------------------------------------------------------------
// Game::Upgrade @ 0x251d4 — Enchant (skill 10): open the item-upgrade menu.
// ---------------------------------------------------------------------------
void Game::Upgrade()
{
    OpenMenu(MENU_UPGRADE);   // Interface::ShowMenu(this+0x4cec, 2) + offers (menus.cpp)
}

// ---------------------------------------------------------------------------
// Game::Teleport @ 0x2ffb4 — Teleport (skill 7): mark every tile cleared WITHOUT paying rewards,
// refill the whole board (spawn flag +0xc380 makes RandomiseTile re-roll skulls away), then drop
// the "new tiles are inert" flag so the fresh board may attack this turn.
// Sources: SKILLS.md row 7, decomp Teleport@0x2ffb4.c, QA_GAME_SKETCH (flare is blue 0,100,ff).
// ---------------------------------------------------------------------------
void Game::Teleport()
{
    for (int i = 0; i < TILE_TYPE_COUNT; ++i) clearedThisChain[i] = 0;   // +0xc36c..+0xc37c
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            Tile &t = board[col][row];
            t.matched = true;                        // +0x88 = 1: cleared, no MatchTile/no pay
            fx().AddFlare(&t, FLARE_TELEPORT);       // 00,64,ff,ff (QA: blue, prose said white)
        }
    if (GetSpecialEnemyCount() > 0)
        fx().QueueAchievement(ACH_BRAVE_SIR_ROBIN, 1.0f);   // 0x41 @ 1.0 — fled a board with monsters
    firstBoard = true;                               // +0xc380 = 1 around the refill
    MoveMatchedTiles();                              // @ 0x2fd7c gravity + skull-free refill
    firstBoard = false;                              // +0xc380 = 0
    ClearInChainOnAll(*this);                        // +0x8a = 0 on all 36 (unconditional here)
    // +0xc150/+0xc154 screen-shake floats — animation, not modelled
}

// ---------------------------------------------------------------------------
// Game::DemoteSpecialEnemy @ 0x2a34c — Exorcise (skill 20): a random special enemy becomes a
// normal one, keeping its attack/defence/health (no gravity — the tile stays put).
// Sources: SKILLS.md row 20, decomp DemoteSpecialEnemy@0x2a34c.c.
// ---------------------------------------------------------------------------
void Game::DemoteSpecialEnemy()
{
    int k = RandI(0, GetSpecialEnemyCount() - 1, RAND_OTHER);   // RandI(0, n-1, 5)
    for (int col = 0; col < BOARD_SIZE && k >= 0; ++col)
        for (int row = 0; row < BOARD_SIZE && k >= 0; ++row) {
            Tile &t = board[col][row];
            if (t.type != TILE_SKULL || t.enemyType == ENEMY_NORMAL) continue;
            if (k != 0) { --k; continue; }
            UpdateEnemyUVDraw(t);                   // 0x2a34c:29 — stream-0 draw with the OLD kind (glow)
            t.enemyType = ENEMY_NORMAL;             // +0x25c = 0 (stats kept: no GenerateStats)
            t.countdown = 0;                        // +0x260 = 0 (ability fuse cleared)
            fx().AddFlare(&t, FLARE_GREEN);         // 00,ff,00,ff
            k = -1;
            break;
        }
    // +0xc150/+0xc154 screen-shake floats — animation, not modelled
}

// ---------------------------------------------------------------------------
// Game::KillSpecialEnemy @ 0x30178 — Banish (skill 21): a random special enemy is cleared with
// NO reward (MatchTile kind 7), then gravity. Sources: SKILLS.md row 21, decomp@0x30178.c.
// ---------------------------------------------------------------------------
void Game::KillSpecialEnemy()
{
    int k = RandI(0, GetSpecialEnemyCount() - 1, RAND_OTHER);
    for (int i = 0; i < TILE_TYPE_COUNT; ++i) clearedThisChain[i] = 0;   // +0xc36c..+0xc37c
    for (int col = 0; col < BOARD_SIZE && k >= 0; ++col)
        for (int row = 0; row < BOARD_SIZE && k >= 0; ++row) {
            Tile &t = board[col][row];
            if (t.type != TILE_SKULL || t.enemyType == ENEMY_NORMAL) continue;
            if (k != 0) { --k; continue; }
            MatchTile(t, MATCH_NO_REWARD);          // MatchTile(7): clear, no loot (chain.cpp)
            fx().AddFlare(&t, FLARE_WHITE);         // ff,ff,ff,ff
            k = -1;
            break;
        }
    MoveMatchedTiles();                             // @ 0x2fd7c
    if (!turnSkillMarker)                           // +0x14d94 == 0 (usual skill-only turn)
        ClearInChainOnAll(*this);                   // +0x8a = 0 on all 36
    // +0xc150/+0xc154 screen-shake floats — animation, not modelled
}

// ---------------------------------------------------------------------------
// Game::PromoteNormalEnemy @ 0x2d454 — Big Game Hunter (skill 23): a random NORMAL enemy is
// promoted to a random special type (stats re-rolled by Tile::Promote). A promoted Fleeing
// enemy immediately jumps to the bottom of its column. Sources: SKILLS.md row 23, decomp.
// ---------------------------------------------------------------------------
void Game::PromoteNormalEnemy()
{
    int k = RandI(0, GetTileCount(TILE_SKULL) - 1 - GetSpecialEnemyCount(), RAND_OTHER);
    for (int col = 0; col < BOARD_SIZE && k >= 0; ++col)
        for (int row = 0; row < BOARD_SIZE && k >= 0; ++row) {
            Tile &t = board[col][row];
            if (t.type != TILE_SKULL || t.enemyType != ENEMY_NORMAL) continue;
            if (k != 0) { --k; continue; }
            promotionEnabled = false;               // +0x1433c = 0 around Promote (only
            t.Promote(ENEMY_NORMAL);                //   SetTimeToPromote reads it; flag = true,
            promotionEnabled = true;                //   random enemy type, stats re-rolled)
            fx().AddFlare(&t, FLARE_PROMOTE);       // f0,96,0a,ff
            if (t.enemyType == ENEMY_FLEEING)       // +0x25c == 0x1f after the promote roll
                JumpDown(t);                        // @ 0x2d3cc (board.cpp)
            k = -1;
            break;
        }
}

// ---------------------------------------------------------------------------
// Game::ExplodeArmour @ 0x31050 — Explosive Armour (skill 27): every enemy on the board takes
// 2x your armour as a direct (fromChain = false) hit. Sources: SKILLS.md row 27, decomp.
// ---------------------------------------------------------------------------
void Game::ExplodeArmour()
{
    const int dmg = armour << 1;                    // +0xc164 * 2
    for (int i = 0; i < TILE_TYPE_COUNT; ++i) clearedThisChain[i] = 0;   // +0xc36c..+0xc37c
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row) {
            Tile &t = board[col][row];
            if (t.type != TILE_SKULL) continue;     // +0xa0 == 1 (specials included)
            fx().AddFlare(&t, FLARE_PROMOTE);       // f0,96,0a,ff
            fx().AddLabel(2, &t, dmg);              // AddSimpleLabel(2, xy, dmg, 0xb, colour)
            AttackSkull(t, dmg, /*fromChain=*/false);   // @ 0x2f630
        }
    MoveMatchedTiles();                             // @ 0x2fd7c
    if (!turnSkillMarker) ClearInChainOnAll(*this); // +0x14d94 == 0 -> clear +0x8a everywhere
    // +0xc150/+0xc154 screen-shake floats — animation, not modelled
}

// ---------------------------------------------------------------------------
// Game::Shock @ 0x30404 — Shock (skill 40): collect every coin & shield in the TOP half of the
// board (rows 0..2). Using Shock and Awe in the same turn spawns a chain tile (combo).
// Sources: SKILLS.md row 40, decomp Shock@0x30404.c (+0x15e72/+0x15e73 pair).
// ---------------------------------------------------------------------------
void Game::Shock()
{
    shockUsed = true;                               // +0x15e72 = 1 at entry (Awe checks it)
    for (int i = 0; i < TILE_TYPE_COUNT; ++i) clearedThisChain[i] = 0;   // +0xc36c..+0xc37c
    explosionSoundPlayed = false;                   // +0x142f4 = 0 once-per-chain blast flag
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < 3; ++row) {         // inner rows 0..2 = top half of the board
            Tile &t = board[col][row];
            if (t.type == TILE_SHIELD || t.type == TILE_COIN)
                MatchTile(t, MATCH_USE_OWN);        // MatchTile(5): pay the tile's own reward
            fx().AddFlare(&t, FLARE_EARTH);         // 73,3c,14,ff on every cell of the half
        }
    MoveMatchedTiles();                             // @ 0x2fd7c
    if (aweUsed) CreateChain();                     // +0x15e73: Shock + Awe in one turn
    if (!turnSkillMarker) ClearInChainOnAll(*this); // +0x14d94 == 0 -> clear +0x8a everywhere
    // +0xc150/+0xc154 screen-shake floats — animation, not modelled
}

// ---------------------------------------------------------------------------
// Game::Awe @ 0x302bc — Awe (skill 41): collect every potion & shield in the BOTTOM half of the
// board (rows 3..5); Shock/Awe same-turn combo spawns a chain tile.
// Sources: SKILLS.md row 41, decomp Awe@0x302bc.c (+0x15e72/+0x15e73 pair).
// ---------------------------------------------------------------------------
void Game::Awe()
{
    aweUsed = true;                                 // +0x15e73 = 1 at entry (Shock checks it)
    for (int i = 0; i < TILE_TYPE_COUNT; ++i) clearedThisChain[i] = 0;   // +0xc36c..+0xc37c
    explosionSoundPlayed = false;                   // +0x142f4 = 0
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 3; row < BOARD_SIZE; ++row) { // inner rows 3..5 = bottom half
            Tile &t = board[col][row];
            if (t.type == TILE_SHIELD || t.type == TILE_POTION)
                MatchTile(t, MATCH_USE_OWN);        // MatchTile(5)
            fx().AddFlare(&t, FLARE_EARTH);         // 73,3c,14,ff on every cell of the half
        }
    MoveMatchedTiles();                             // @ 0x2fd7c
    if (shockUsed) CreateChain();                   // +0x15e72: Shock + Awe in one turn
    if (!turnSkillMarker) ClearInChainOnAll(*this); // +0x14d94 == 0 -> clear +0x8a everywhere
    // +0xc150/+0xc154 screen-shake floats — animation, not modelled
}

// ---------------------------------------------------------------------------
// Game::RemoveSkill @ 0x24eec — take one skill out of the +0xc358 array (Wish is single-use):
// shift every later entry down one slot, then drop the count (+0xc368). The original leaves the
// removed pointer in the now-stale last slot; pop_back() is the rebuild's equivalent.
// Sources: decomp RemoveSkill@0x24eec.c (exact shift order).
// ---------------------------------------------------------------------------
void Game::RemoveSkill(Skill *s)
{
    const int count = static_cast<int>(skills.size());   // +0xc358 array, +0xc368 count
    if (count <= 0 || s == nullptr) return;
    int idx = -1;
    for (int i = 0; i < count; ++i)
        if (skills[static_cast<std::size_t>(i)].get() == s) { idx = i; break; }
    if (idx < 0) return;                            // not in the array: no-op
    for (int j = idx; j < count - 1; ++j) {
        skills[static_cast<std::size_t>(j)] = std::move(skills[static_cast<std::size_t>(j + 1)]);
        // orig: Interface::PositionSkill(this+0x4cec, j) — re-lays-out the skill bar (UI only)
    }
    skills.pop_back();                              // +0xc368 - 1 (destroys `s` — see RESULT.md)
}

// NOTE: the six board-collection effects (Slash/Fireball/Charge/Wish/Hack/Greed) moved to
// src/skill_board_effects.cpp (R12) — no stub bodies left in this file.

} // namespace dr
