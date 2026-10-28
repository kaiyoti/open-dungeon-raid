// Combat system: incoming enemy damage (Game::ProcessAttack states 5/6), DoCounterattack,
// EndOfTurn, CheckEndGame (+ the ProcessVampires / ProcessCripples / GetScore helpers they call).
// Sources: analysis/combat/PLAYER_DAMAGE.md, analysis/combat/END_OF_TURN.md and the decomp of
// ProcessAttack @ 0x311b8, DoCounterattack @ 0x30f38, EndOfTurn @ 0x3289c, CheckEndGame @ 0x2f960.
// The animation state machine (overlay alpha, flare flight, timers) is not modelled: the whole
// damage sequence collapses into ApplyIncomingDamage. Rendering goes through Platform (fx()).
#include "dr/game.h"
#include "dr/character_class.h"
#include "dr/rng.h"
#include "dr/skill.h"
#include <algorithm>

namespace dr {

namespace {

// decomp argument of MatchTile: 5 = "use the tile's own type" (same constant as chain.cpp)
constexpr TileType MATCH_USE_OWN = static_cast<TileType>(5);

// GUESS: Platform AddFlare only takes a colour kind; original colours are these RGBA bytes
// (END_OF_TURN.md step 4: ice-blue {100,0xe6,0xff,0xff} thaw, orange {0xf0,0x96,0x0a,0xff} burn).
constexpr int FLARE_ICEBLUE = 2;
constexpr int FLARE_ORANGE  = 3;

} // namespace

// ---------------------------------------------------------------------------
// Game::ApplyIncomingDamage @ 0x311b8 (ProcessAttack states 5/6) — the armour/health
// resolution of the turn's incoming damage total (+0xc244, written by InitialiseFlares).
// ---------------------------------------------------------------------------
void Game::ApplyIncomingDamage()
{
    // state-4 gate (PLAYER_DAMAGE.md:15,40): state 5 is entered only when armour >= 1 —
    // at 0 armour ProcessAttack @0x311b8 goes 4 -> 6 directly (decomp state 5 block at :240
    // runs blunting 0xc22c and spikes 0xc228). Captured before the break loop mutates armour.
    const bool armourPath = armour >= 1;
    const int hpBefore = hp;                 // M9 event bookkeeping (task A3), no game effect
    const int incomingTotal = incomingDamage;

    // ---- state 5: armour absorbs point-for-point (+0xc164 vs +0xc244) ----
    int blocked = std::min(armour, incomingDamage);
    incomingDamage -= armour;

    int broken = 0;                                       // shield points lost
    bool defiler = GetEnemyCount(ENEMY_DEFILER) > 0;      // 29: every blocked point breaks
    for (int i = 0; i < blocked; ++i)
        if (defiler || RandI(0, 100, RAND_OTHER) < armourBreakChance)   // +0xc188 (base 50, min 5)
            ++broken;
    if (broken > 0) {
        armour -= broken;
        fx().OnArmourChanged(armour);                     // Interface::UpdateShieldValue
        fx().ShieldBreak(broken);                         // M9 event hook (task A3) — ProcessAttack @ 0x311b8 state 5 break
        InialiseShieldShatter();                          // board.cpp @ 0x2535c (orig spelling)
        // S3 step 3: the armour-break counterpart of the ARMOUR(15) clank above. ProcessAttack
        // @ 0x311b8 ELF 0x21640 "movs r1,#0x10; bl QueueSound" (reached when the break count sb>0);
        // InialiseShieldShatter @ 0x2535c is the animation only and queues no sound (board.cpp:295).
        fx().QueueSound(SOUND_ARMOUR_BREAK);              // 16
    } else if (blocked > 0) {
        // S3 step 3: armour absorbed the hit but NO shield point broke -> ARMOUR clank(15).
        // ProcessAttack @ 0x311b8 ELF 0x21bc6 "mov r0,r4; movs r1,#0xf; bl QueueSound", reached
        // when the break count (sb) is 0; the broken>0 counterpart plays ARMOUR_BREAK(16) @ELF
        // 0x21640 (InialiseShieldShatter @ 0x2535c). blocked>0 <=> armour>=1 (state-5 armour path).
        fx().QueueSound(SOUND_ARMOUR);                    // 15
    }
    if (armourPath) {
        // ProcessAttack @0x311b8:253 label at the 5->6 transition (type 2, the "-N DMG" health
        // label). ASM (ELF 0x21572-0x21598): r0=this, r1=2 (type), r2=r8 (pos), stack [sp]=0xc,
        // [sp+4]=float, [sp+0xc]=this+0xc200; r3 = rsbs(min(incomingDamage, +0xc164)) = -blocked
        // at creation. Then 0x215be-0x215da: r1 = incoming - +0xc164 (pre-break armour); if >0
        // SetNumber(label, incoming-armour) (number := +damage, veneer bl 0x4fb88), else state 7
        // keeps -blocked. The label draws "-|number| DMG" (sign byte -1): emu attack plan
        // incoming 5/armour 4 -> "-1" (damage), kill plan incoming 2/armour 3 -> "-2" (blocked).
        // Rebuild value = post-armour incoming when >0, else blocked (the page prepends the minus).
        fx().AddLabel(2, nullptr, incomingDamage > 0 ? incomingDamage : blocked);
    }

    // Spikes/Blunting run only on the armour-hit path (PLAYER_DAMAGE.md note: with 0 armour the
    // sequence goes 4 -> 6 directly, so this whole block is skipped).
    if (armourPath && blunting > 0) {                      // +0xc22c "Blunting: Reduce enemies' attack"
        bool soundPlayed = false;
        for (int c = 0; c < BOARD_SIZE; ++c)
            for (int r = 0; r < BOARD_SIZE; ++r) {
                Tile &t = At(c, r);
                if (t.type != TILE_SKULL || t.inChain || t.attack < 2) continue;   // +0xa0/+0x8a/+0xa8
                if (!soundPlayed) {                       // decomp plays one QueueSound for the batch
                    fx().QueueSound(SOUND_BLUNT);         // id 30 SOUND_BLUNT (D6-U16: ELF 0x2169c
                    // "movs r1,#0x1e; bl QueueSound" — stripped args recovered, blunt1-3.ogg)
                    soundPlayed = true;
                }
                t.attack = std::max(1, t.attack - blunting);
                fx().AddLabel(4, &t, blunting);           // D6-U13: 0x311b8:326 AddSimpleLabel
                // (type 4, pos = Tile::GetAttackLabelPosition, num = Game+0xc22c = BLUNTING,
                // colour 14 white, scale 1.0) — ELF 0x216f2 "movs r1,#4 ... movs r1,#0xe".
                fx().AddParticles(&t, 3);                 // D6-U17: 0x311b8:327 AddParticles(tile,
                // 3) — ELF 0x216f6 "movs r2,#3"; kind 3 = attack-label pos, white (Particles::
                // InitialiseFromTile @0x5af68:41-47). Kind 2/3 both consume 81 stream-0 draws.
            }
    }

    if (armourPath && spikes > 0) {                        // +0xc228 "Spikes: Damage attackers"
        for (int i = 0; i < TILE_TYPE_COUNT; ++i) clearedThisChain[i] = 0;   // +0xc36c.. (decomp zeroes)
        bool soundPlayed = false;
        for (int c = 0; c < BOARD_SIZE; ++c)
            for (int r = 0; r < BOARD_SIZE; ++r) {
                Tile &t = At(c, r);
                if (t.type != TILE_SKULL || t.inChain) continue;   // ProcessAttack @0x311b8:350
                if (!soundPlayed) {
                    fx().QueueSound(SOUND_SWORD_START);   // 3 (decomp QueueSound(this,3))
                    soundPlayed = true;
                }
                EnemyType subtype = t.enemyType;          // +0x25c, read before the hit
                fx().AddLabel(5, &t, spikes);             // AddSimpleLabel(5, xy, spikes, colour 0x10)
                if (AttackSkull(t, spikes, /*fromChain=*/false) && subtype == ENEMY_SPIKY)
                    fx().QueueAchievement(ACH_TOUCHE, 100.0f);   // 0x2b: killed a spiky enemy
            }
        MoveMatchedTiles();                               // gravity + refill after the spike kills
        for (int c = 0; c < BOARD_SIZE; ++c)
            for (int r = 0; r < BOARD_SIZE; ++r) At(c, r).inChain = false;
        DrainTokens();                                    // V23: spike-kill loot lands now
    }

    if (incomingDamage < 1) {                             // armour ate it all: done (state 7)
        if (poisonThisTurn > 0)
            fx().AddLabel(7, nullptr, poisonThisTurn);    // D6-U15: 0x311b8:268 AddSimpleLabel
        // (type 7, num = Game+0x14d84 = poison, colour 15 purple (169,76,202), scale 0.5) —
        // ELF 0x21b14-0x21b3c; type 7 DREW in the decomp path (not in {1,3,8,9,10}).
        poisonThisTurn = 0;                               // poison display cleared
        fx().IncomingResolved(incomingTotal, blocked, broken, hpBefore, hp);  // M9 hook (task A3), state 7 exit
        return;
    }

    // ---- state 6: health takes the rest ----
    // S4 step 2: DAMAGE(17) at the head of the health-damage branch. ProcessAttack @ 0x311b8
    // ELF 0x219fc "movs r1,#0x11; bl QueueSound" — the first instruction of the branch that
    // draws the health label, SetPoison, ProcessVampires and does hp(0xc158) -= 0xc244
    // (decomp :391-412, ELF 0x21a00-0x21a8a). Queued once per turn, only when incomingDamage
    // (0xc244 after armour) >= 1; the armour-ate-it-all path (0xc244 < 1, state 7) queues
    // nothing (ELF 0x21b14). The armour-blocked paths queue 15/16 above (S3).
    fx().QueueSound(SOUND_DAMAGE);              // 17
    // +0x14d88 is the poison LEVEL = max(current, incoming) (PLAYER_DAMAGE.md correction),
    // applied every EndOfTurn and cured by potions — not a "max seen" stat.
    poisonLevel = std::max(poisonLevel, poisonThisTurn);
    fx().AddLabel(2, nullptr, incomingDamage);            // 0x311b8:392 health-damage label at state
    // 6 entry — K19 (Y7): args recovered from disassembly (capstone, objdump has no ARM target),
    // ELF 0x21a00-0x21a20: r1=2 (SimpleLabelType 2), r2=r5+0xf0 = Game+0xc0f0 (&GLfloat2, not a
    // tile -> nullptr), r3=*(Game+0xc244) = incomingDamage, [sp]=0xb (colour 11), [sp+4]=0.5f
    // (s16 literal @ELF 0x219d8, same scale the poison label reuses) -> AddSimpleLabel(type,
    // pos, value, colour, scale). Was: UNKNOWN(type, ProcessAttack@0x311b8:392).
    // Interface::SetPoison(poisonLevel) — UI, no Platform hook (NEED: OnPoisonChanged)
    if (poisonThisTurn > 0) fx().AddLabel(7, nullptr, poisonThisTurn);  // D6-U15: 0x311b8:401
    // poison label, num = Game+0x14d84 (poison), colour 15 purple, scale 0.5 (ELF 0x21a42-0x21a5c)
    poisonThisTurn = 0;
    ProcessVampires();
    hp = std::max(0, hp - incomingDamage);                // +0xc158, floor 0
    fx().IncomingResolved(incomingTotal, blocked, broken, hpBefore, hp);  // M9 hook (task A3), state 6 exit
}

// ---------------------------------------------------------------------------
// Game::ProcessVampires @ 0x2d18c — every living vampire heals itself for its own attack
// after the player took the turn's incoming damage.
// ---------------------------------------------------------------------------
void Game::ProcessVampires()
{
    EnemyType rival = ActiveClass().MortalEnemy();        // race's mortal enemy (+0x14db4 = race)
    if (getenv("DR_VAMP")) {                              // K4 temp probe
        for (int c = 0; c < BOARD_SIZE; ++c) for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &v = At(c, r);
            if (v.type == TILE_SKULL && v.enemyType == ENEMY_VAMPIRE)
                std::fprintf(stderr, "  [PV] vamp c%d r%d hp=%d max=%d m88=%d atk=%d def=%d class=%d\n",
                             c, r, v.health, v.maxHealth, (int)v.matched, v.attack, v.defence,
                             (int)activeClass);
        }
    }
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &t = At(c, r);
            if (t.matched || t.type != TILE_SKULL || t.enemyType != ENEMY_VAMPIRE) continue;
            if (rival == ENEMY_VAMPIRE) {
                t.AddHealth(t.GetAttack());               // Tile::AddHealth @ 0x63a5c (raises maxHealth, K11)
            } else if (t.health < t.maxHealth) {          // +0xb0 / +0xb4
                t.health = std::min(t.maxHealth, t.health + t.GetAttack());
            } else {
                continue;        // K1: full health and not the rival -> ProcessVampires @0x2d18c:26-28
                                 // `goto LAB_0002d1de`: NO label, NO sound (seed 10 m51 drew 1 extra)
            }
            fx().AddLabel(0x1a, &t, t.GetAttack());       // label 26, colour 0x22
            fx().QueueSound(SOUND_BOOST);                 // 0x17
        }
}

// ---------------------------------------------------------------------------
// Game::DoCounterattack @ 0x30f38 — the "Counter-Attack" upgrade (skill 4): every attacker
// takes its OWN attack value as damage. Runs from ProcessAttack state 3 when
// counterAttackPending (+0xc382) is set; it never touches player HP.
// ---------------------------------------------------------------------------
void Game::DoCounterattack()
{
    for (int i = 0; i < TILE_TYPE_COUNT; ++i) clearedThisChain[i] = 0;   // +0xc36c..

    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &t = At(c, r);
            if (t.type != TILE_SKULL || t.inChain) continue;   // attackers = skulls outside the chain
            fx().AddLabel(2, &t, t.GetAttack());               // damage number, colour 0xb
            AttackSkull(t, t.GetAttack(), /*fromChain=*/false);
        }

    MoveMatchedTiles();
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) At(c, r).inChain = false;
    DrainTokens();            // V23: counterattack loot lands after the refill
}

// ---------------------------------------------------------------------------
// Game::ProcessCripples @ 0x2f558 — werewolf (race 7, TRAIT_CRIPPLE) racial power: a normal
// enemy whose armour (+0xac) exceeds its HP (+0xb0) dies outright at every EndOfTurn.
// ---------------------------------------------------------------------------
void Game::ProcessCripples()
{
    if (Race() != RACE_WEREWOLF) return;                  // +0x14db4 + activeClass*0x184 == 7
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &t = At(c, r);
            if (t.type != TILE_SKULL || t.enemyType != ENEMY_NORMAL) continue;
            if (t.matched || t.defence <= t.health) continue;
            MatchTile(t, MATCH_USE_OWN);                  // decomp passes 5: loot + achievements
            fx().AddLabel(0x24, &t, 0);                   // label 36, colour 0x2c
            fx().QueueAchievement(ACH_BITES_THE_DUST, 2.0f);   // 0x35 @ 2.0f
        }
}

// ---------------------------------------------------------------------------
// Game::GetScore @ 0x2f868 — run score. Classic: resources + turn + skill ranks.
// Sprint: quartered resources (rounded up), doubled on the 100-turn survival.
// Pretzel: the collected-tiles total (chain.cpp's unknown15e2c).
// ---------------------------------------------------------------------------
int Game::GetScore() const
{
    if (challenge) {                                      // +0x14335
        if (mode == MODE_PRETZEL) return unknown15e2c;    // +0x14330 == 1
        if (mode == MODE_SPRINT) {                        // +0x14330 == 0
            int s = (xp + xpHundreds + 3) / 4             // (x+3)>>2 in the decomp
                  + (gold + goldHundreds + 3) / 4
                  + (upgradePoints + upHundreds + 3) / 4;
            return survivedSprint ? s * 2 : s;            // +0x15e71: survived 100 sprint turns
        }
    }
    int s = xp + xpHundreds + gold + goldHundreds + upgradePoints + upHundreds + turn;
    for (const auto &sk : skills) s += sk->rank * 100;    // Skill+0x88 rank
    return s;
}

// ---------------------------------------------------------------------------
// Game::CheckEndGame @ 0x2f960 — true when the run is over. Death path: Last Stand revive
// (+0x15cec) can save you once; sprint challenge + 100 turns survived (+0x15e71) is a win.
// ---------------------------------------------------------------------------
bool Game::CheckEndGame()
{
    if (gameOver) return true;                            // +0xce64

    bool victory = survivedSprint && challenge && mode == MODE_SPRINT;
    if (!victory) {
        if (hp > 0) return false;                         // +0xc158: still alive
        if (Race() != RACE_HUMAN && GetEnemyCount(ActiveClass().MortalEnemy()) > 0)
            fx().QueueAchievement(ACH_NEMESIS, 100.0f);   // 0x28: died with your rival on the board
        if (reviveUsed) {                                 // +0x15cec Last Stand (skills.cpp)
            reviveUsed = false;
            carryRevive = 0;                              // +0x15ced
            hp = MaxHp();                                 // full heal: +0xc15c + +0xc160
            fx().OnHealthChanged(hp);
            fx().QueueSound(SOUND_ABILITY);               // 0x13
            fx().AddLabel(0x1c, nullptr, 0);              // label at the poison icon, colour 0x24
            fx().QueueAchievement(ACH_BACK_FROM_DEAD, 100.0f);   // 0x3d
            return false;                                 // back from the dead
        }
    } else {
        fx().QueueAchievement(ACH_SURVIVOR, 100.0f);      // 0x4c: 100 sprint turns survived
    }

    // ---- game over bookkeeping ----
    // CONFIRMED: GameOverScreen is embedded at Game+0xce60 (Game ctor @0x28c8c:89) and
    // GameOverScreen::Initialise @0x457d4:63 writes `this[4] = 1` = Game+0xce64; CheckEndGame
    // calls it @0x2f960:155 and gates on +0xce64 at @0x2f960:27.
    gameOver = true;
    ++classRuns[activeClass];                             // +0x14e04 per-class death count
    classKills[activeClass] += enemiesKilled;             // +0x14e08 per-class kill total
    int score = GetScore();
    if (!challenge) {
        // ScoreScreen::AddScore(name, turn, xp, gold, upgrade pts, score, difficulty) — UI.
        // Per-difficulty bests at class+0x14e0c/10/14 (normal/hard/harder; easy has none):
        if (difficulty >= DIFF_NORMAL && difficulty <= DIFF_HARDER) {
            int &best = classBestScore[activeClass][difficulty - DIFF_NORMAL];
            if (best < score) best = score;
        }
    }
    // ScoreScreen::SetSkills / SerialiseClasses @ 0x24d98 / GameOverScreen::Initialise — UI/save,
    // not modelled here.
    fx().OnGameOver();
    // S3 step 3: GameOverScreen::Initialise @ 0x457d4:153-156 (called by CheckEndGame @ 0x2f960:155)
    // queues LOSE(33) when killed (GameOverScreen+0x164 == 0) or LEVELUP(12) when the sprint
    // challenge was survived (this+0x164 != 0). `victory` is the rebuild's escaped test (:235).
    fx().QueueSound(victory ? SOUND_LEVELUP : SOUND_LOSE);
    fx().QueueAchievement(ACH_MISSION_ACCOMPLISHED, 100.0f);   // 0x43
    return true;
}

// ---------------------------------------------------------------------------
// Game::EndOfTurn @ 0x3289c — the per-turn tick, in the original order (END_OF_TURN.md 1-15).
// Enemy->player damage is NOT here: it runs in ProcessAttack (ApplyIncomingDamage).
// ---------------------------------------------------------------------------
void Game::EndOfTurn()
{
    ++turn;                                               // +0xc
    if (CheckEndGame()) return;

    // 2. skills: recharge while no enemy type 0xf (ENEMY_MAGE) is on the board; all deactivate
    for (auto &sk : skills) {                             // Skill*[] +0xc358, count +0xc368
        if (GetEnemyCount(ENEMY_MAGE) == 0) {             // +0x14330? no: enemy type 0xf
            if (sk->charge == sk->maxCharge - 1)
                fx().QueueSound(SOUND_ABILITY_READY);     // 10 on full
            sk->charge = std::min(sk->charge + 1, sk->maxCharge);   // Skill+0x90 cap +0x8c
        }
        sk->active = false;                               // Skill::Deactivate @ 0x5ff10 (+0x80)
    }
    fx().OnSkillsChanged();                               // Interface::UpdateSkills

    // 3. per-turn resets (bytes whose meaning is not modelled keep their decomp offsets in comments)
    coinsToSkullsFlag = false;                            // +0x15e18 (siblings +0x15e19..0x15e1c not modelled)
    // +0x14da9/+0x14daa: per-turn flags, not modelled
    shockUsed = false;                                    // +0x15e72 (decomp EndOfTurn line 68)
    aweUsed = false;                                      // +0x15e73 — Shock+Awe combo only within one turn
    if (runeActive && !carryRune) {
        // selection arrow back to green (Arrow quad +0x3668) — rendering, not modelled
    }
    runeActive = false;                                   // +0x15cd9
    carryRune = 0;                                        // +0x15cda
    berserkActive = (carryBerserk != 0);                  // +0x15cef <- +0x15cf0
    carryBerserk = 0;
    counterAttackPending = false;                        // +0xc382 = 0 (EndOfTurn @0x3289c:81)
    doubleDamage   = (carryDoubleDamage != 0);            // +0xc234..0xc237 <- +0xc238..0xc23b
    doubleShields  = (carryDoubleArmour  != 0);
    doublePotions  = (carryDoubleHealth  != 0);
    doubleCoins    = (carryDoubleGold    != 0);
    carryDoubleDamage = carryDoubleArmour = carryDoubleHealth = carryDoubleGold = 0;
    stealTokenMode = (carrySteal != 0);                   // +0xc383 <- +0xc384
    carrySteal = 0;
    for (int i = 0; i < TILE_TYPE_COUNT; ++i) clearedThisChain[i] = 0;   // +0xc36c..0xc37c
    // +0xc150/+0xc154 <- 0/1.0f: shake/spawn timer floats — animation, not modelled

    // 4. fire & frost tick: frozen tiles thaw (and skip their burn, keeping the flame);
    //    burning enemies take +0x268 once, then the flame goes out.
    bool thawSoundPlayed = false;
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &t = At(c, r);
            if (t.frozen) {                               // +0x265
                fx().AddFlare(&t, FLARE_ICEBLUE);
                t.frozen = false;                         // Tile::SetFreezeVisibility(false)
                if (!thawSoundPlayed) {
                    fx().QueueSound(SOUND_SHATTER);       // 0x16 once
                    thawSoundPlayed = true;
                }
            } else if (t.flaming) {                       // +0x266
                fx().AddFlare(&t, FLARE_ORANGE);
                if (t.type == TILE_SKULL) {
                    fx().AddLabel(9, &t, t.flameDamage);  // burn label, colour 0x12
                    if (t.health - t.flameDamage < 1) {   // +0xb0
                        t.health = 0;
                        AttackSkull(t, 0, /*fromChain=*/false);   // death/reward path
                        if (Race() == RACE_DRAGONKIN)     // END_OF_TURN.md correction: race 8
                            fx().QueueAchievement(ACH_BURNINATOR, 2.0f);   // 0x36
                    } else {
                        t.health -= t.flameDamage;
                        fx().AddParticles(&t, 2);
                    }
                }
                t.flaming = false;                        // Tile::SetFlameVisibility(false)
            }
        }

    // 5. regen (+0xc230), capped at base + bonus max HP
    if (regen >= 1 && hp < MaxHp()) {
        fx().QueueSound(SOUND_POTION);                    // 4
        hp = std::min(hp + regen, MaxHp());
        fx().AddLabel(3, nullptr, regen);                 // heal label at the health bar, 0x0d
    }

    // 6. player poison: the level repeats every turn until cured by potions
    if (poisonLevel > 0) {                                // +0x14d88
        fx().QueueSound(SOUND_POISON);                    // 0x1d
        hp -= poisonLevel;
        if (hp < 0) {                                       // EndOfTurn @0x3289c:187-190
            hp = 0;
            fx().QueueAchievement(ACH_POISONED, 100.0f);    // 0x24 @ 0x42c80000
        }
        fx().OnHealthChanged(hp);
        fx().AddLabel(7, nullptr, poisonLevel);           // poison label, colour 0x0f
        if (CheckEndGame()) return;
    }

    // 7. enemy poison tick (EndOfTurn @0x3289c:204-230). K5: every skull tile runs
    //    Tile::GetPoison @0x62b0c — NOT gated on +0x24c: the tick damage is the pre-rotation
    //    +0x248 and the rotation +0x248<-+0x24c, +0x24c<-+0x250, +0x250<-0 must run whenever
    //    +0x248 != 0 (measured native seed 2 m51-54: (2,0,0) still ticks r0=2 and resets to
    //    (0,0,0); the old `t.poison == 0` guard left poisonAddedThisTurn set — seed 2 m51).
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &t = At(c, r);
            if (t.type != TILE_SKULL) continue;
            const int d = t.GetPoison();             // rotate + return +0x248 (@0x62b0c)
            if (d == 0) continue;
            fx().AddLabel(7, &t, d);
            if (t.health - d < 1) {
                MatchTile(t, MATCH_USE_OWN);              // dies: loot + refill slot
                if (Race() == RACE_ELF)                   // END_OF_TURN.md correction: race 1
                    fx().QueueAchievement(ACH_FLESH_WOUND, 2.0f);   // 0x2f
            } else {
                t.health -= d;
                fx().AddParticles(&t, 2);
            }
        }

    // 8. abilities: clear the fired guard, cripples, then every special enemy acts once
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) At(c, r).abilityFired = false;   // +0x264
    ProcessCripples();
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &t = At(c, r);
            if (t.type == TILE_SKULL && t.enemyType != ENEMY_NORMAL)
                t.ActivateAbility();                      // Tile::ActivateAbility @ 0x64c78
        }

    // 9. byte history +0x15cee <- +0x15cec <- +0x15ced (revive carry chain, K11).
    // EndOfTurn @0x3289c:262-264: +0x15cee = +0x15cec; +0x15cec = +0x15ced; +0x15ced = 0.
    // (berserk +0x15cef = +0x15cf0 runs earlier, @0x3289c:78-79, rebuilt at step 5 above.)
    reviveAvailable = reviveUsed;                       // +0x15cee = +0x15cec
    reviveUsed = (carryRevive != 0);                    // +0x15cec = +0x15ced
    carryRevive = 0;                                    // +0x15ced

    // 10-15. only while alive: gravity refill, flag history, tutorial hints, autosave
    if (!CheckEndGame()) {
        MoveMatchedTiles();                               // per-column gravity + RandomiseTile refill
        for (int c = 0; c < BOARD_SIZE; ++c)
            for (int r = 0; r < BOARD_SIZE; ++r) At(c, r).inChain = false;   // +0x8a

        oneHitSkulls = (carryOneHitSkulls != 0);          // +0x1433d <- +0x1433f (Trap)
        forceCoinMode = (carryForceCoin != 0);            // +0x1433e <- +0x14340 (Treasure Chamber)
        carryOneHitSkulls = 0;
        carryForceCoin = 0;
        firstBoard = false;                               // +0xc380
        firstBoardAvoidSword = false;                     // +0xc381
        promoteCountdown = std::max(0, promoteCountdown - 1);   // +0x14338 (purpose unknown)

        if (helpState & 0x8000) {                         // +0x103d8 tutorial bits (HelpMenu+0x38)
            for (int c = 0; c < BOARD_SIZE; ++c)
                for (int r = 0; r < BOARD_SIZE; ++r)
                    if (At(c, r).type == TILE_SKULL) {
                        fx().ShowHelp(15, &At(c, r));     // first enemy seen
                        goto tutorialDone;                // decomp: first-enemy scan, one hit
                    }
        }
      tutorialDone:
        if (helpState & 0x200000) {                       // A24 FIX: EndOfTurn @0x3289c:309
            // gates the N21 scan on `+0x103d8 << 10 < 0` = BIT 21 (0x200000), not bit 10.
            for (int c = 0; c < BOARD_SIZE; ++c)
                for (int r = 0; r < BOARD_SIZE; ++r)
                    if (At(c, r).type == TILE_SKULL && At(c, r).enemyType != ENEMY_NORMAL) {
                        fx().ShowHelp(21, &At(c, r));     // first special enemy seen
                        goto specialDone;
                    }
        }
      specialDone:
        // CONFIRMED: no tutorial-bit write in EndOfTurn (it only reads +0x103d8 at :291,:309) —
        // HelpMenu::Initialise @0x48348:15-21 XORs the shown bit out of HelpMenu+0x38
        // (= Game+0x103d8) and calls SetHelpState(false) when the word reaches 0.

        turnSkillMarker = false;                          // +0x14d94, EndOfTurn @0x3289c:330
        // CONFIRMED EndOfTurn @0x3289c:331-340: challenge && mode==0 && turn==100
        // (MODE_SPRINT = 0, enums.h); read by GetScore @0x2f868:42 / CheckEndGame @0x2f960:30.
        survivedSprint = (challenge && mode == MODE_SPRINT && turn == 100);   // +0x15e71

        if (!CheckEndGame())
            SerialiseState();                             // autosave every turn (save.cpp)
    }
}

} // namespace dr
