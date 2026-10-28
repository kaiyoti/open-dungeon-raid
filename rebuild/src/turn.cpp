// Turn flow: chain linking rules, chain attack power, incoming enemy damage, and PlayMove (one turn).
// Written by dungeon_raid_master from the decomp; see analysis/board/CHAIN.md, combat/PLAYER_DAMAGE.md.
#include "dr/game.h"
#include "dr/character_class.h"
#include <algorithm>
#include <cstdlib>

namespace dr {

// MatchTile `as` argument value 7 = clear without reward (CHAIN.md; also local in
// skill_board_effects.cpp); the trophy tap uses it exactly like ProcessInput @0x330c0:382.
constexpr TileType MATCH_NO_REWARD = static_cast<TileType>(7);

// Lich's phylactery: enemy 32 (ENEMY_LICH, ENUMS.md:165; rolled by Tile::Promote @0x64e3c:14
// RandI(1,0x20) — all classes) with a running +0x260 charge (set on lethal damage by
// Game::AttackSkull @0x2f630:33-38: charge=1, attack/defence=0, HP=1; cleared on resurrect by
// Tile::ActivateAbility @0x64c78 case 0x20). Only coins and other charged phylacteries can link
// to it (Y6 emu: tasks/Y6/y6_emu.json S1-S5, S12-S14).
static bool IsPhylactery(const Tile &t) { return t.type == TILE_SKULL && t.enemyType == 32 && t.countdown != 0; }

// Tile::CanMatchType @ 0x645ac — may tile t be appended after the current chain's last tile?
bool Game::CanMatchType(const Tile &t, bool fromInput) const
{
    if (t.frozen && t.type != TILE_SKULL) return false;          // frozen non-enemies can't be picked
    if (chain.empty()) return true;
    if (t.specialVariant == SPECIAL_TILE_CHAIN) return true;     // +0xa4 == 0: "chain" special links anything
    if (t.specialVariant == SPECIAL_TILE_TROPHY) return false;   // +0xa4 == 1 path, traced: with a
    // non-empty chain CanMatchType @0x645ac returns false when this tile's +0xa4 is 1 (structure
    // verified by brace depth: `if (a4!=0) { if (a4!=1) {...normal...} return false; }` — the
    // fall-through `return false` @ :86 is hit by a4==1). The original ALSO returns false when the
    // LAST tile's a4==1 (same line, via `if (last.a4 != 1)` @ :33); unreachable to reproduce here:
    // type-6 tiles only exist with a4==0 (chain link) or a4==1 (trophy), so no type-6 a4==3 tile
    // could pass the rebuild's later type-equality test.
    const Tile &last = *chain.back();
    if (!fromInput && last.type == TILE_SWORD && last.arrow) return true;   // arrow: next tile may be anything
    if (last.specialVariant == SPECIAL_TILE_CHAIN) return true;
    if (chain.size() >= 2) {                                     // K1: tile after the arrow's bridged tile
        auto it = chain.end(); --it; --it;
        const Tile &prev = **it;
        if (prev.type == TILE_SWORD && prev.arrow) {
            // CanMatchType @0x645ac:77-82 (else of "prev is not an arrow"): only a skull or a sword
            // may follow the bridged tile; a charged phylactery lich (+0x25c==0x20 && +0x260!=0)
            // falls through to `return false` @ :86 (Y6 emu S14: arrow->coin->phyl = false;
            // S9/S10: sword/skull after the bridge = true).
            if (t.type == TILE_SKULL && t.enemyType == 32 && t.countdown != 0) return false;
            return t.type == TILE_SKULL || t.type == TILE_SWORD;
        }
    }
    bool tPhyl = IsPhylactery(t), lastPhyl = IsPhylactery(last);
    // coin-wildcard + mortal-enemy branches @0x645ac:58-67,70-75 (Y6 emu S1-S5,S12,S13:
    // coin<->phyl true both directions, phyl<->phyl true, phyl<->sword false; a mortal lich
    // (+0x260==0) is NOT a phylactery and falls through to plain type equality, S6/S8).
    if (tPhyl)    return last.type == TILE_COIN || lastPhyl;
    if (lastPhyl) return t.type == TILE_COIN;
    if (t.type == last.type) return true;
    return (t.type == TILE_SKULL && last.type == TILE_SWORD) || (t.type == TILE_SWORD && last.type == TILE_SKULL);
}

// ProcessTileSelection @ 0x2c8dc — chain attack (+0xc23c): (base + bonus) once if the chain holds an
// attackable enemy (halved while your mortal enemy is Freezing and one is on the board), plus per
// unbroken sword swordDamage (x5 magic sword), swords doubled by Boost Damage (+0xc234).
int Game::ComputeChainAttack() const
{
    int attack = 0; bool enemySeen = false;
    for (const Tile *t : chain) {
        if (t->type == TILE_SWORD && !t->broken) {
            int per = t->magicSword ? swordDamage * 5 : swordDamage;
            attack += per * (doubleDamage ? 2 : 1);
        } else if (t->type == TILE_SKULL && !enemySeen && !IsPhylactery(*t)) {
            int base = baseDamage + attackBonus;                   // +0xc180 + +0xc184
            if (classes[activeClass]->MortalEnemy() == ENEMY_FROZEN && GetEnemyCount(ENEMY_FROZEN) != 0)
                base /= 2;
            attack += base;
            enemySeen = true;
        }
    }
    return attack;
}

// InitialiseFlares @ 0x2a548 — incoming damage = sum of attack of every enemy with tile+0x8a==0
// (spawn flag: +0x8a = "spawned this cascade", set ONLY on refill spawns by MoveMatchedTiles
// @0x2fd7c:79, carried by Tile::operator= @0x27cd4:31 through gravity; cleared board-wide in
// MatchTiles @0x31bcc:188-195, EndOfTurn @0x3289c:271-278 and the ability handlers — D1) and
// not frozen; this turn's poison = sum of attack of Poisonous (9) enemies among them.
void Game::InitialiseIncoming()
{
    incomingDamage = 0; poisonThisTurn = 0;
    for (auto &col : board)
        for (const Tile &t : col) {
            // D1 (K9/PROVENANCE refute K13's drag-write claim): the native gate is tile+0x8a =
            // "spawned this cascade", written only on refill spawns (MoveMatchedTiles @0x2fd7c:79;
            // zeroed Tile ctors @0x63f44:35 / @0x6407c:35, copied @0x27cd4:31 / @0x28800:38).
            // Drag/press membership is NOT a gate — the chain lives in the +0xb78c list; +0x89
            // (set ProcessInput @0x330c0:132 / ProcessTileSelection @0x2c8dc:318, cleared
            // ProcessInput @0x330c0:209,227 / ProcessTileSelection @0x2c8dc:86,174) is only the
            // highlight byte. rebuild inChain = the +0x8a model (cleared board-wide at MatchTiles
            // before this gate runs, so only spawn flags remain — same as native attack time).
            if (t.type != TILE_SKULL || t.inChain || t.frozen) continue;   // +0x8a gate: spawned-tile flag (D1)
            fx().Attacker(t);   // M9 event hook (task A3): this enemy is in the attack set — InitialiseFlares @ 0x2a548
            incomingDamage += t.GetAttack();
            if (t.enemyType == ENEMY_POISONOUS) poisonThisTurn += t.GetAttack();
        }
}

// Game::CountAttackers @ 0x24e2c — enemies (tile+0x8a == 0 = not spawned this cascade, not
// frozen; D1: +0x8a is the spawn flag, NOT chain membership) with attack > 0.
int Game::CountAttackers() const
{
    int n = 0;
    for (auto &col : board)
        for (const Tile &t : col)
            if (t.type == TILE_SKULL && !t.inChain && !t.frozen && t.GetAttack() > 0) ++n;   // +0x8a gate: spawned-tile flag (D1)
    return n;
}

// Path rules: >= 3 tiles, each 8-neighbour of the previous, no repeats, each passes CanMatchType.
bool Game::IsValidMove(const std::vector<Cell> &path) const
{
    return path.size() >= 3 && IsValidPrefix(path);
}

// The per-tile part of IsValidMove without the length rule (split out so the web demo can check a
// chain while it is being drawn; behaviour of IsValidMove unchanged).
bool Game::IsValidPrefix(const std::vector<Cell> &path) const
{
    if (path.empty()) return false;
    Game &self = const_cast<Game &>(*this);
    std::list<Tile *> saved; saved.swap(self.chain);
    bool ok = true;
    for (size_t i = 0; i < path.size() && ok; ++i) {
        const Cell &c = path[i];
        if (c.col < 0 || c.col >= BOARD_SIZE || c.row < 0 || c.row >= BOARD_SIZE) { ok = false; break; }
        Tile &t = self.At(c.col, c.row);
        for (Tile *x : self.chain) if (x == &t) ok = false;
        if (!ok) break;
        if (i > 0 && (std::abs(c.col - path[i-1].col) > 1 || std::abs(c.row - path[i-1].row) > 1)) { ok = false; break; }
        if (!CanMatchType(t)) { ok = false; break; }
        self.chain.push_back(&t);
    }
    self.chain.swap(saved);
    return ok;
}

// The deferred half of the turn: the enemy attack (ProcessAttack @ 0x311b8 states 1..8: incoming
// computed at state 1/2, armour absorb at 5, health at 6) and EndOfTurn @ 0x3289c (state 8).
// V12: runs immediately after MatchTiles when the move opened no menu, and from ChooseMenu when a
// menu opened — in the original the open menu gates the whole game block of Game::Process @ 0x339ac
// (ProcessInput + ProcessAttack run only while +0x5ae4/+0x5c00/+0x5c94 == 0), so the attack sees
// the post-menu stats: seed 12345 move 28, the shop armour purchase (+0xc164 0->1) lands at the
// menu close (frame 1182) and the attack (state 1, frame 1184) absorbs 1 hit with 1 break roll.
void Game::FinishTurnAfterMenus()
{
    // K9: Life Leech lands here — Process @0x339ac: the heal block (:290-320) runs inside the
    // no-open-menu game block (:82) and BEFORE ProcessAttack (:481): while +0xc6e0 > 0, when the
    // label animation finishes, hp = min(hp + +0xc6e0, +0xc160 + +0xc15c), +0xc6e0 = 0,
    // Interface::SetHealth (:319) and QueueSound(this, 4) = SOUND_POTION (:320).
    if (pendingHeal > 0) {
        hp = std::min(hp + pendingHeal, MaxHp());
        pendingHeal = 0;                                    // +0xc6e0 = 0 (:318)
        fx().QueueSound(SOUND_POTION);
    }
    if ((turn > 4 || !HavePerk(PERK_UNEXPECTED)) && CountAttackers() > 0) {
        // V24: the attack animation consumes stream 0: Game::InitialiseFlares @ 0x2a548 (called
        // from ProcessAttack at attack start) adds 2 eye flares per attacker (skull, not spawn-
        // flagged +0x8a, not frozen, lich-with-fuse skipped — D1: chain membership is not a gate,
        // the press tile counts; NO attack>0 test, unlike CountAttackers); then 1 impact draw at
        // state 4->5/6 (0x311b8:170 /:178) and a 2nd at 5->6 (:251) when armour>0.
        int flareAttackers = 0;
        for (int c = 0; c < BOARD_SIZE; ++c)
            for (int r = 0; r < BOARD_SIZE; ++r) {
                const Tile &t = At(c, r);
                if (t.type != TILE_SKULL || t.inChain || t.frozen) continue;   // +0x8a gate: spawned-tile flag (D1)
                if (t.enemyType == ENEMY_LICH && t.countdown != 0) continue;   // 0x2a548:49-51
                ++flareAttackers;
            }
        CosmeticAttackFlares(flareAttackers);
        if (counterAttackPending) {
            // K5: with +0xc382 set (Skill::Activate case 4 Counterattack @0x60210:33) the attack
            // machine goes state 2 -> 3 (ProcessAttack @0x311b8:92-108, QueueSound, args stripped)
            // INSTEAD of 2 -> 4, so the state-4 impact Rands (:170/:178), the state-5/6 absorb
            // (:251 Rand + blunting/spikes) and the player-damage labels never run; the state-3
            // timer then does state = 7 + DoCounterattack @0x30f38:75-100 (:130-147). Measured
            // native seed 2 m31: 8 attack flares, 0 impact draws, 4x(AddSimpleLabel type 2 +
            // AttackSkull -> AddParticles kind 2), hp/armour untouched by the attack.
            // S4 step 2: ATTACK_HIT(27) at the state 2 -> 3 transition itself. ProcessAttack
            // @ 0x311b8 ELF 0x2199c "movs r1,#0x1b; bl QueueSound" — queued once per turn when
            // +0xc382 != 0 (the 0x21996 "ldrb r2,[+0xc382]; cmp r2,#0; beq" skips it on the
            // 2 -> 4 path), before the state-3 timer runs DoCounterattack.
            fx().QueueSound(SOUND_ATTACK_HIT);  // 27
            DoCounterattack();
        } else {
            CosmeticAttackImpact(armour > 0);
            InitialiseIncoming();
            ApplyIncomingDamage();
        }
        // No CheckEndGame gate here: ProcessInput @ 0x330c0 runs the attack state machine to
        // state 8 and then LAB_00033208 calls EndOfTurn @ 0x3289c unconditionally — EndOfTurn
        // does ++this+0xc FIRST and only then CheckEndGame, which gates the rest of itself.
        // Measured native seed 9 move 75: hp 1->0 at attack state 7, state reaches 8, EndOfTurn
        // increments turn 74->75 (tools/emu/v17_probe.py).
    }
    // K5: the original's game block (Game::Process @0x339ac) runs ProcessAttack and EndOfTurn
    // only while no menu is open (the same V12 gate that freezes the attack state machine): a
    // menu opened mid-attack — spike/counterattack loot landing -> AddResource -> ShowMenu, as
    // in the native seed 5 m80 level-up — freezes EndOfTurn until ChooseMenu closes it, so the
    // EndOfTurn skill recharge runs on the POST-apply skill objects. Measured native: SetSkill
    // apply precedes EndOfTurn entry (tools/emu/k5_charge_probe.py); the rebuild recharged the
    // old skills first (seed 5 m80: 15/3 vs native 16/4).
    if (menuPending) { endOfTurnPending = true; return; }
    EndOfTurn();
    DrainTokens();            // V23: EndOfTurn ability-kill loot lands before the next move
}

// K12 — the frame at which a pending flame/spiky preview label lands (0 = never armed here).
// The label is armed at RELEASE (ProcessInput @0x330c0:265-284: +0x146b0 = 0, +0x146b4 = 1,
// +0x146a0/+0x14a15 = 1) and Game::Process @0x339ac:374-394 (flame) / :442-473 (spiky) then
// integrates it once per frame — the release frame included (ProcessInput runs first, the label
// block later in the same Game::Process call):
//   v += dt * DAT_000340f0 (10.0);  p += v * dt;  p clamped to DAT_000340f4 (1.0);  land at p >= 1.
// At the harness frame dt = 1/30 (tools/emu/game.py FRAME_US = 33333) that is integration #11 —
// measured on the original (tools/emu/k12_probe.py, seeds 7 m52 and 10 m61: the label lands 10
// frames after the release; p = 0.944 after #10, clamped to 1.0 at #11).
static int PendingLabelFrame()
{
    const float dt = 1.0f / 30.0f;
    float p = 0.0f, v = 1.0f;
    for (int frame = 1; frame <= 1000; ++frame) {
        v += dt * 10.0f;                 // DAT_000340f0
        p += v * dt;
        if (p > 1.0f) p = 1.0f;          // Process @0x339ac:380-381 clamp to DAT_000340f4
        if (p >= 1.0f) return frame;     // :394 / :459 — the label's damage lands this frame
    }
    return 1000;
}

// One turn, as ProcessInput @ 0x330c0 sequences it: release chain -> MatchTiles; then if enemies may
// attack (turn > 4 or no perk "Unexpected") and there are attackers -> attack sequence (incoming
// damage applied, ProcessAttack states 5/6) ; then EndOfTurn. Returns false if the move was invalid.
// V12: when MatchTiles leaves a reward menu pending, the attack + EndOfTurn are deferred to
// ChooseMenu (the original freezes the attack state machine +0xbc5c while the menu is open).
bool Game::PlayMove(const std::vector<Cell> &path)
{
    if (gameOver || menuPending || !IsValidMove(path)) return false;   // a reward menu blocks moves
    // V24: the press on path[0] initialises the preview counters (ProcessInput @ 0x330c0:90-161)
    // and every tile the drag adds accumulates them (ProcessTileSelection @ 0x2c8dc:335-368); the
    // release consumes a stream-0 draw for each live one, right after MatchTiles (:266/:277/:315).
    // Captured BEFORE MatchTiles mutates the tiles.
    int previewDamage = baseDamage + attackBonus;                     // +0xc180 + +0xc184
    if (ActiveClass().MortalEnemy() == ENEMY_FROZEN && GetEnemyCount(ENEMY_FROZEN) != 0)
        previewDamage /= 2;                                           // 0x330c0:104-108
    bool attackPrev = false;                                          // +0xc240
    int  flameSum   = 0;                                              // +0x1469c
    bool spikyPrev  = false;                                          // +0x14a10>0 at press end
    int  c23c       = 0;                                              // +0xc23c preview damage (UCV gate)
    // V34: Game::UpdateCrossVisibility @ 0x2c5e8 runs on press (ProcessInput @0x330c0:141, list=1)
    // and on every drag add (ProcessTileSelection @0x2c8dc:384, gated 0 < +0xc23c). Each call adds
    // one SimpleLabel per selected tile with Tile::GetDamageType @0x644bc == 1 (label type 0x1e,
    // :117) or == 3 (type 0x23, :84) — both draw 1 stream-0 (not in {1,3,8,9,10} @0x5fbc4).
    // Measured native seed 7 move 54: 6 draws (one dt-1 skull seen by all 6 calls; the path's two
    // 0x1e meat-shields are dt-2, the rest dt-0) — tools/emu/v34_probe.py/v34_probe2.py.
    chain.clear();
    handledChainTiles.clear();   // K1: the press erases the +0xb794 set (ProcessTileSelection @0x2c8dc:90-101)
    for (std::size_t i = 0; i < path.size(); ++i) {
        Tile &t = At(path[i].col, path[i].row);
        // K1 (ProcessTileSelection @0x2c8dc:216-224): when the last chain tile is an ARROW sword
        // and the new tile is neither a sword nor a skull, the drag still links it (CanMatchType
        // rule A @0x645ac:27) but records it in the Game+0xb794 set and takes NO preview from it.
        // MatchTiles (@0x31bcc, rebuild chain.cpp `handledChainTiles` skip) then never clears or
        // rewards a recorded tile: measured native seed 6 move 2, the bridged potion at (r4,c2)
        // survives while the 6 swords clear (6 refill draws, 0 AddToken).
        const bool bridged = !chain.empty() && chain.back()->type == TILE_SWORD
                             && chain.back()->arrow && t.type != TILE_SWORD && t.type != TILE_SKULL;
        if (bridged) {
            handledChainTiles.push_back(&t);
        } else {
        const bool skull = t.type == TILE_SKULL && !(t.enemyType == ENEMY_LICH && t.countdown != 0);
        if (!attackPrev && skull) {                                   // :101-103 / PTS:349-361
            attackPrev = true;
            c23c += previewDamage;                                    // +0xc23c += +0xc180++0xc184
        } else if (t.type == TILE_SWORD && !t.broken) {               // PTS:334-350 (+0xa0==4, +0x26c==0)
            c23c += swordDamage * (t.magicSword ? 5 : 1) * (doubleDamage ? 2 : 1);   // +0xc17c, +0x240, +0xc234
        }
        if (t.flaming && !t.flamePulse) flameSum += t.flameDamage;    // :127 / PTS:363-366 (+0x268)
        if (t.type == TILE_SKULL && t.enemyType == ENEMY_SPIKY)       // 0x330c0:105-107 (press) / 0x2c8dc:367-369 (drag): ANY spiky skull in the chain sets +0x14a14
            spikyPrev = true;                                         // K3: was i==0-only
        }
        // K9: Life Leech (item stat2, +0xc220 = sum of Item+0x9c) — the queued heal
        // +0xc6e0 = (int)((float)+0xc23c * ((float)+0xc220 / 100.0f)) is recomputed on the press
        // (ProcessInput @0x330c0:156) and after EVERY drag add (ProcessTileSelection @0x2c8dc:377),
        // bridged tiles included (that write sits in the common tail of the add loop).
        pendingHeal = static_cast<int>(static_cast<float>(c23c) *
                                       (static_cast<float>(upgradeGateLevel) / 100.0f));
        // C1: the native does NOT set +0x8a on drag/press tiles (K9 probe: the only +0x8a writes
        // are MoveMatchedTiles @0x2fd7c:79 spawn, Tile::operator= @0x27cd4:31 gravity copy, and the
        // board-wide clears MatchTiles @0x31bcc:188-195 / EndOfTurn @0x3289c:271-278 / ability
        // handlers). Native chain membership lives in the +0xb78c list and the +0x89 highlight
        // byte (ProcessInput @0x330c0:132 press / ProcessTileSelection @0x2c8dc:318 drag add).
        // The old rebuild-model flag (K13: "if (i > 0) t.inChain = true;") gave chain MEMBERSHIP
        // semantics to +0x8a; it was cleared board-wide in MatchTiles (chain.cpp:283-286) before
        // any gate ran, so it was invisible to the attacker gates — and it is now removed: the
        // rebuild's +0x8a lifecycle is exactly the native's (spawn-set, gravity-copied, cleared).
        // Gates that read it: InitialiseFlares @0x2a548:34, CountAttackers @0x24e2c:20,
        // ProcessAttack spikes @0x311b8:350, ThrowSkull @0x2eeb0:41, DoCounterattack @0x30f38:27.
        chain.push_back(&t);
        if (i == 0 || c23c > 0) {
            int nLabels = 0;
            for (auto it = chain.begin(); it != chain.end(); ++it) {
                const int dt = ChainDamageType(*this, **it, it);
                if (dt == DAMAGE_BLOCKED || dt == DAMAGE_UPPERCUT) ++nLabels;   // decomp returns 1 / 3
            }
            CosmeticCrossVisibility(nLabels);
        }
    }
    const bool flamePrev = flameSum > 0;
    chainAttack = ComputeChainAttack();
    // ProcessInput @0x330c0 release block :247-259 — set the marker and reset the per-move
    // counters immediately before MatchTiles.
    turnSkillMarker = true;                     // +0x14d94 = 1 (chain accepted, >2 tiles)
    // S4 step 4: the chain attack sound. ProcessInput @ 0x330c0:229-253 (release, touch state 2)
    // — the loop over ALL +0xb78c chain nodes sets bVar2 = any tile+0xa0==1 (skull), bVar14 = any
    // tile+0xa0==4 (sword); gate 2 < (uint)(chainLen - Game+0xb7a4) (0xb7a4 = 0: chain >= 3, the
    // accepted-chain gate that also runs MatchTiles :260). Then bVar2 -> QueueSound 0x1b
    // ATTACK_HIT (ELF 0x237b2); else bVar14 -> QueueSound 0x1a ATTACK_MISS (ELF 0x234d4, tutorial
    // HelpMenu :251-255 is UI, not modelled). Mixed skull+sword chains hit (bVar2 wins). Queued
    // before the counter resets + MatchTiles (:256-260), same order as the original.
    {
        bool anySkull = false, anySword = false;
        for (Tile *t : chain) {
            if (t->type == TILE_SKULL) anySkull = true;
            else if (t->type == TILE_SWORD) anySword = true;
        }
        if (anySkull)      fx().QueueSound(SOUND_ATTACK_HIT);    // 27
        else if (anySword) fx().QueueSound(SOUND_ATTACK_MISS);   // 26
    }
    killsCounted     = 0;                       // +0x14d90
    killedAttackSum  = 0;                       // +0x15cf4
    tilesSpawned     = 0;                       // +0x15e20
    enemiesSpawned   = 0;                       // +0x15e24
    // K12: the flame/spiky label is armed HERE, at release (ProcessInput @0x330c0:265-284), and
    // lands PendingLabelFrame() frames later (Process @0x339ac:394/:459) — tokens still in flight
    // then are gated if it kills (:176). DrainTokens honours tokenLabelFrame until it has landed.
    tokenLabelFrame = (flameSum > 0 || spikyPrev) ? PendingLabelFrame() : 0;
    MatchTiles();
    // ProcessInput @0x330c0:286-306 — post-MatchTiles kill epilogue (runs before the attack
    // machine, i.e. also when MatchTiles left a reward menu pending).
    if (killsCounted > 0) {                     // +0x14d90 gate :286
        AddRevengeHealth();                     // Vengeful +0x14d90 attack (enemy_abilities.cpp)
        if (killsCounted > 14 && Race() == RACE_HALFLING) {   // :286 0xe<+0x14d90, :287 race(+0x14db4)==6
            Item &arm = *items[ARMOUR];         // Game+0xc250 = GetItem(2) @0x24610 (slot 2 = ARMOUR)
            int v = arm.stats[11] + 1;          // Item::IncreaseStat @0x4dbb8: stats[0xb] (armour,
                                                // this+0xc0) += ItemUpgrade::GetStatUpgrade(0xb, game),
                                                // which is 1 (GetStatUpgrade @0x51888 case 0xb -> 1)
            arm.stats[11] = v & ~(v >> 31);     // clamp >= 0 (decomp `x & ~(x >> 0x1f)`); MakeStats text
                                                // (presentation) not modelled
            UpdateStats();                      // :289
            fx().AddLabel(0x22, nullptr, 0);    // :290-292 AddSimpleLabel(0x22, fixed pos DAT_000336bc/c0,
                                                // value 0, colour 0x2a) — presentation
            fx().QueueAchievement(ACH_THE_BIGGER_THEY_ARE, 10.0f);       // :293 0x34 @ 0x41200000
            if (coinsToSkullsFlag)              // :294 this[0x15e18] != 0
                fx().QueueAchievement(ACH_WHAT_DOESNT_KILL_YOU, 100.0f); // 0x39 @ 0x42c80000
        }
    }
    if (berserkActive && killedAttackSum > 0) { // +0x15cef Berserk "kills heal", ProcessInput:294
        int healed = hp + killedAttackSum;
        hp = healed < MaxHp() ? healed : MaxHp();          // +0xc158 cap +0xc160 + +0xc15c
        fx().QueueSound(SOUND_POTION);          // QueueSound(this, 4)
        fx().OnHealthChanged(hp);               // Interface::SetHealth
        fx().AddLabel(0x1d, nullptr, killedAttackSum);   // AddSimpleLabel(0x1d, hp-icon, +0x15cf4, 0x25)
    }
    // ProcessInput @0x330c0:311-312 — a release with no attack preview (+0xc240 == 0) drops the
    // queued Life-Leech heal; with a preview the label-anim params are inited (the :315 stream-0
    // draw is the existing CosmeticPreview one).
    if (!attackPrev) pendingHeal = 0;
    chain.clear();
    CosmeticPreview(flamePrev, spikyPrev, attackPrev);   // 0x330c0:266/277/315 (after MatchTiles)
    // V30: the two preview labels are NOT cosmetic-only. Game::Process @ 0x339ac:394-409 lands
    // the flame label's damage when its animation completes (after MatchTiles, before the
    // attack): hp (+0xc158) -= +0x1469c (the chain's flame sum), floor 0, achievement 0x23
    // "Toasted" on death, SetHealth, sound 0x11, then CheckEndGame gates the rest of Process.
    if (flameSum > 0) {
        hp -= flameSum;
        if (hp < 0) {                                       // Process @0x339ac:396-399
            hp = 0;
            fx().QueueAchievement(ACH_TOASTED, 100.0f);     // 0x23 @ 0x42c80000
        }
        fx().OnHealthChanged(hp);
        fx().QueueSound(SOUND_DAMAGE);                   // 0x11
        if (CheckEndGame()) return true;
    }
    // Game::Process @ 0x339ac:459-473 — the spiky label: hp -= +0x14a10 (= +0xc23c, the FULL press
    // preview damage incl. the sword term, set at 0x330c0:158-161 / 0x2c8dc:379-382 when +0x14a14),
    // floor 0, achievement 0x22 "Impaled" on death, SetHealth, sound 0x19, CheckEndGame.
    if (spikyPrev) {
        hp -= c23c;                                                   // K3: was previewDamage (skull term only)
        if (hp < 0) {                                                 // Process @0x339ac:461-464
            hp = 0;
            fx().QueueAchievement(ACH_IMPALED, 100.0f);               // 0x22 @ 0x42c80000
        }
        fx().OnHealthChanged(hp);
        fx().QueueSound(SOUND_SLASH);                    // 0x19
        if (CheckEndGame()) return true;
    }
    if (CheckEndGame()) return true;
    // K12: the label has landed above (or was never armed) — tokens whose flight outlives it are
    // not gated any more, so they land now, still before the attack (ProcessInput @0x330c0 runs
    // the attack only after the release block; V23 kept every token ahead of it).
    tokenLabelFrame = 0;
    DrainTokens();
    if (menuPending) { attackPending = true; return true; }   // V12: menu freezes the attack
    FinishTurnAfterMenus();
    return true;
}

// ---------------------------------------------------------------------------
// Trophy collection — Game::TapTile, headless form of ProcessInput @0x330c0:325-389.
// ---------------------------------------------------------------------------
// In the original a trophy is collected by a touch release with EXACTLY ONE matched tile that is
// a trophy (+0xa0 == 6 && +0xa4 == 1, :330-332); any other single tile just clears the list
// (LAB_000332ba). This path runs NO attack and NO EndOfTurn: ProcessInput starts the attack only
// from 0x5f74 == 0 (the "MatchTiles ran, attack pending" flag — MatchTiles @0x31bcc:55 sets it,
// EndOfTurn's exit @0x330c0:77 clears it) and the trophy path never calls MatchTiles, so in the
// rebuild's turn-collapsed model (PlayMove already ran attack + EndOfTurn for the previous
// chain) the collection alone is the whole move; the turn counter does not advance. Stream-0
// order is preserved as in the original: the previous turn's EndOfTurn refill draws happened
// inside that PlayMove, the trophy's MoveMatchedTiles refill draws happen here (TROPHY.md §5.8).
// Returns true when the tap resolved as a trophy collection; false = not a trophy (harness must
// only tap trophies: the original's press on a non-trophy may draw stream-0 preview labels).
bool Game::TapTile(int col, int row)
{
    if (gameOver || menuPending) return false;      // an open menu freezes the game block (Process)
    if (col < 0 || col >= BOARD_SIZE || row < 0 || row >= BOARD_SIZE) return false;
    // :325-326 flame-preview reset (+0xc6e0 = 0, no stream-0 draw) / :327-329 recount — both are
    // invisible in this model (the tap press never armed a preview: a trophy is not type 1/4).
    Tile &t = At(col, row);
    if (t.type != TILE_SPECIAL || t.specialVariant != SPECIAL_TILE_TROPHY) return false;  // :330-332
    CharacterClass &active = ActiveClass();
    // :333-335 path 1 — 50% chance to LEVEL UP the active class instead of unlocking a new one.
    // The roll (RandI(0,1,stream5)) is skipped entirely — NO RNG draw — when the active class is
    // the Adventurer (type byte +0x14db0 == 0) or already level 10 (level byte +0x14e00 > 9).
    bool levelActive = false;
    if (active.level < 10 && active.klass != CLASS_ADVENTURER && RandI(0, 1, RAND_OTHER) != 0) {
        levelActive = true;                        // -> LAB_00033802
    } else {
        // :343-364 path 2 — collect every still-locked class (level byte == 0; class 0 can never
        // be 0, so the Adventurer is never picked here) and unlock one of them at random.
        std::vector<int> locked;
        for (int i = 0; i < CLASS_COUNT; ++i)
            if (classes[i]->level == 0) locked.push_back(i);
        if (!locked.empty()) {
            int idx = locked[RandI(0, static_cast<int>(locked.size()) - 1, RAND_OTHER)];  // :367
            classes[static_cast<std::size_t>(idx)]->LevelUp();   // 0 -> 1 (@0x3600c)
            ShowUnlockMenu(classes[static_cast<std::size_t>(idx)]->klass, 1);   // :369 Show(sel, 1)
        } else {
            levelActive = true;                    // :380 goto LAB_00033802
        }
    }
    if (levelActive && active.level < 10) {        // LAB_00033802:337 (Adventurer 1..10 included)
        active.LevelUp();                          // CharacterClass::LevelUp @ 0x3600c
        ShowUnlockMenu(active.klass, active.level);// Show(klass, NEW level) :340-342
    }
    MatchTile(t, MATCH_NO_REWARD);                 // :382 MatchTile(tile, 7): +0x88 = 1, no reward
    MoveMatchedTiles();                            // :383 gravity + refill (stream-0 draws)
    if (CheckEndGame()) return true;               // :384-386
    SerialiseState();                              // :388
    SerialiseClasses();                            // :389 class levels persist across runs (C1)
    return true;
}

// UnlockMenu::Show(ClassType, int) @ 0x671ec — record the popup the original would show (the demo
// reads Game::unlockPopups; strings/sound are presentation and are not ported). No game state.
void Game::ShowUnlockMenu(int classType, int level)
{
    unlockPopups.push_back({classType, level});
}

} // namespace dr
