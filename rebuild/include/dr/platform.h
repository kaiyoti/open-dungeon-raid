// Presentation hooks the original game logic calls (sounds, labels, particles, popups, achievements).
// The reconstruction keeps game logic headless: Game calls these through a Platform object, and the
// default implementation does nothing. A real front end (renderer/audio/UI) can override them.
#pragma once
#include "enums.h"
#include "cosmetic_draws.h"

namespace dr {

struct Tile;

// FoldoutMenu menu kinds opened by Game::AddResource (not a SWIG enum; values from the native code).
enum MenuType { MENU_LEVEL_UP = 0, MENU_SHOP = 1, MENU_UPGRADE = 2 };

// V24: the default (headless) implementations below consume the stream-0 (RAND_COSMETIC) draws
// that the original's presentation functions consume (analysis/state/RNG_STREAM0.md) — RNG
// bookkeeping only, no game logic. A real front end that OVERRIDES AddLabel/AddParticles/AddFlare
// must call the base implementation, otherwise stream 0 drifts and the trophy countdown diverges.
struct Platform {
    virtual ~Platform() = default;
    virtual void QueueSound(Sound) {}
    virtual void QueueAchievement(Achievement, float /*progress, 100 = unlocked*/) {}
    virtual void ShowHelp(int /*g_helpText index*/, const Tile * /*anchor or null*/) {}
    // A8: labelRise = the label's rise-speed draw (SimpleLabel::Initialise @ 0x5fbc4:27
    // Rand(0.1,0.2,0) -> +0x344), set by the base AddLabel; the web front end forwards it as
    // EV_LABEL p5 (x10000) so the renderer rises each label at the ORIGINAL speed. -1 = no draw.
    float labelRise = -1.0f;
    virtual void AddLabel(int labelKind, const Tile *, int /*value*/) { CosmeticLabel(labelKind, &labelRise); }
    virtual void AddParticles(const Tile *, int kind) { CosmeticParticles(kind); }
    virtual void AddFlare(const Tile *, int /*colour kind*/) { CosmeticFlare(); }
    virtual void ShowMenu(MenuType) {}          // level-up / shop / upgrade foldout (MENUS.md)
    // V29 harness trace hook (tools/emu/v29_probe.py pairs with drcli DR_TOKEN_TRACE): one call
    // per Game::AddToken @ 0x2a934. Default does nothing — test harness only, no game effect.
    virtual void TokenAdded(const Tile & /*t*/, int /*tokenTileType*/, int /*tokenType*/,
                            int /*value*/, bool /*bonus*/) {}
    virtual void OnHealthChanged(int /*hp*/) {}
    virtual void OnArmourChanged(int /*armour*/) {}
    virtual void OnSkillsChanged() {}
    virtual void OnGameOver() {}
    // ---- M9 event-log hooks (task A3) — presentation plumbing, NO game effect, NO RNG draw.
    // Default no-op; the web Platform (web/api.cpp) records them into one ordered event log.
    // MatchTile @ 0x2ac40 sets tile+0x88 = 1 (cleared/matched this move).
    virtual void TileMatched(const Tile & /*t*/) {}
    // MoveMatchedTiles @ 0x2fd7c gravity: tile in column `col` moves from row `fromRow` to
    // `toRow`; spawned = true for a refill tile that starts above the board (fromRow < 0).
    virtual void TileFell(int /*col*/, int /*fromRow*/, int /*toRow*/, bool /*spawned*/) {}
    // Game::AddToken @ 0x2a934: a token queued, flying FROM tile `from` (the killed tile for
    // enemy loot, the cleared tile otherwise); `flight` = K12 landing frames (misc.cpp
    // TokenFlight, shield tokens modelled, others 1); value -1 = dead mana token (no AddResource).
    // `u5` = the token's stream-0 flight draws (Token::InitialiseFromTile @0x653d0:64,66,78,81,111
    // — dir x/y, scale, accel, skull scale; see cosmetic_draws.h) for the web renderer's token
    // flight (A4b); nullptr when the token died (no draws). Presentation only.
    virtual void TokenQueued(const Tile & /*from*/, int /*tokenTileType*/, int /*tokenType*/,
                             int /*value*/, bool /*bonus*/, int /*flight*/,
                             const float * /*u5*/ = nullptr) {}
    // InitialiseFlares @ 0x2a548 (turn.cpp InitialiseIncoming): this enemy is in the attack set.
    virtual void Attacker(const Tile & /*t*/) {}
    // ProcessAttack @ 0x311b8 states 5/6 (combat.cpp ApplyIncomingDamage): the incoming total
    // `incoming` resolved — armour absorbed `blocked`, `broken` shield points lost,
    // player hp hpBefore -> hpAfter.
    virtual void IncomingResolved(int /*incoming*/, int /*blocked*/, int /*broken*/,
                                  int /*hpBefore*/, int /*hpAfter*/) {}
    // ProcessAttack @ 0x311b8 state 5 armour break -> Game::InialiseShieldShatter @ 0x2535c:
    // `armourLost` shield points broke this attack.
    virtual void ShieldBreak(int /*armourLost*/) {}
};

} // namespace dr
