// Chain system: Game::MatchTiles (run the released chain), Game::MatchTile (clear one tile and pay
// its reward), Game::AttackSkull (a hit lands on an enemy). Sources: analysis/board/CHAIN.md plus
// the decomp of MatchTiles @ 0x31bcc, MatchTile @ 0x2ac40, AttackSkull @ 0x2f630 (exact numbers).
// Tile types (CHAIN.md review correction): 0 potion, 1 skull, 2 coin, 3 shield, 4 sword.
// Rendering (UV/text refresh, particles, flares, labels, popups) goes through Platform (fx()).
#include "dr/game.h"
#include "dr/character_class.h"
#include "dr/rng.h"
#include <algorithm>
#include <iterator>
#include <list>
#include <set>

namespace dr {

namespace {

// decomp argument of MatchTile: 5 = "use the tile's own type", 7 = clear but pay nothing (Cocky).
constexpr TileType MATCH_USE_OWN    = static_cast<TileType>(5);
constexpr TileType MATCH_NO_REWARD  = static_cast<TileType>(7);

// +0x25c == 0x20 and +0x260 != 0: chain hits are ignored; a killing blow strips it instead
// (armour/HP zeroed, +0x26e set, it survives). GUESS: enums.h index 32 = ENEMY_LICH (CHAIN.md
// calls 0x20 "armoured"); ENEMY_ARMOURED in enums.h is 0x1b.
constexpr EnemyType ENEMY_UNKILLABLE = static_cast<EnemyType>(0x20);
// CHAIN.md whiff case "your race's mortal enemy"; enums.h index 14 = ENEMY_CHAOTIC (GUESS).
constexpr EnemyType ENEMY_MORAL_RIVAL = static_cast<EnemyType>(0xe);
// 0x1b ENEMY_ARMOURED: whiff unless the next chain tile is above it. 0x1e ENEMY_MEAT_SHIELD: other
// enemies whiff while one lives (enums.h names match the observed behaviour).

// GUESS: Platform AddFlare only takes a colour kind; original colours are these RGBA bytes.
constexpr int FLARE_RED    = 0;   // 0xff,0x00,0x00,0xff (3x3 blasts)
constexpr int FLARE_YELLOW = 1;   // 0xff,0xff,0x00,0xff (stripped 0x20 enemy)

// Tile::GetRewards @ 0x646e4 — loot list for a killed enemy (NEED: no tile.h decl, file-local
// mirror; list element = TileType). n = RandI(15,25,RAND_TILE) rolled once per call.
void ChainGetRewards(const Tile &t, std::list<TileType> &out)
{
    auto push = [&out](TileType tt, int count) {
        for (int i = 0; i < count; ++i) out.push_back(tt);
    };
    int n = RandI(15, 25, RAND_TILE);
    switch (t.enemyType) {
    case ENEMY_GOLDEN:                                    // 3: drops its weight in coins
        push(TILE_COIN, n);
        break;
    case ENEMY_HEALER: {                                  // 4: coins, potions and a few skulls
        int potions = RandI(n / 4, n / 3, RAND_TILE);
        n -= potions;
        int coins = RandI(3 * n / 4, n / 4, RAND_TILE);
        push(TILE_POTION, potions);
        push(TILE_COIN, coins);
        push(TILE_SKULL, n - coins);
        break;
    }
    case ENEMY_THIEF: {                                   // 13: mostly coins
        int coins = RandI(3 * n / 4, n / 2, RAND_TILE);
        push(TILE_COIN, coins);
        push(TILE_SKULL, n - coins);
        break;
    }
    case ENEMY_ARMOURED: {                                // 27: shields, coins, skulls
        int shields = RandI(n / 4, n / 3, RAND_TILE);
        n -= shields;
        int coins = RandI(3 * n / 4, n / 4, RAND_TILE);
        push(TILE_SHIELD, shields);
        push(TILE_COIN, coins);
        push(TILE_SKULL, n - coins);
        break;
    }
    default: {                                           // every other enemy (incl. <3 and >27)
        int coins = RandI(3 * n / 4, n / 3, RAND_TILE);
        push(TILE_COIN, coins);
        push(TILE_SKULL, n - coins);
        break;
    }
    }
}

// MatchTile's 3x3 blast: kamikaze enemy @ 0x2ac40 LAB_0002b050, explosive potion @ LAB_0002ad62.
// Rule per neighbour: skulls are cleared only when enemyType == 0 (normal); every non-skull is
// cleared, but a potion carrying its own explosive flag (+0x244) gets no flare (GUESS: it is
// expected to go off through its own MatchTile instead). Then the centre cell flares once.
void Blast3x3(Game &g, Tile &centre, bool skipCentre)
{
    // V33: the native blast walks the 3x3 COLUMN-major — outer loop over columns, inner over rows
    // (MatchTile @0x2ac40:127-145 potion blast `uVar10` = col-1..col+1 outer / `iVar5` = row-1..
    // row+1 inner, pGVar8 += 0xea0 = row stride; the kamikaze blast @0x2ac40:70-95 is identical).
    // The match ORDER decides clearedThisChain[kind] at the bonus-roll gate (:189), so the walk
    // direction is game logic, not presentation (seed 12345 m78: row-major skipped one roll).
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy) {
            if (skipCentre && dx == 0 && dy == 0) continue;
            int c = centre.col + dx, r = centre.row + dy;
            if (c < 0 || c >= BOARD_SIZE || r < 0 || r >= BOARD_SIZE) continue;
            Tile &cell = g.At(c, r);
            if (cell.type == TILE_SKULL) {
                if (cell.enemyType != ENEMY_NORMAL) continue;   // only normal enemies are blasted
                g.fx().AddFlare(&cell, FLARE_RED);
            } else if (cell.type != TILE_POTION || !cell.explosivePotion) {
                g.fx().AddFlare(&cell, FLARE_RED);
            }
            g.MatchTile(cell, MATCH_USE_OWN);                   // recursive clear (guard stops loops)
        }
    g.fx().AddFlare(&centre, FLARE_RED);
    // original also writes +0xc150/+0xc154 (shake/spawn timer floats) here — animation, not modelled
}

// bonus-roll chance pairs at Game+0xc1d8..+0xc1f4, laid out {coin, potion, skull, shield} in
// memory (MatchTile switch @ 0x2ac40 LAB_0002b0f4: case 0 -> c1e0/e4, 1 -> c1e8/ec, 2 -> c1d8/dc,
// 3 -> c1f0/f4; PERKS_FLAWS.md confirms c1d8 = gold chance, c1f0 = shield chance).
// The [1] half is written ONLY by UpdateStats @ 0x26c94 as boostXCount * Skill::GetStatChance(),
// which the rebuild keeps in the boostXChance fields (game.h +0xc1dc/e4/ec/f4) — bonusChance[][1]
// is never written, so reading it made every chance 20 and dropped the stat-boost bonus (V10 seed 29).
int BonusChanceFor(const Game &g, int kind)
{
    switch (kind) {
    case TILE_POTION: return g.bonusChance[TILE_POTION][0] + g.boostVitChance;   // +0xc1e0 + +0xc1e4
    case TILE_SKULL:  return g.bonusChance[TILE_SKULL][0] + g.boostStrChance;    // +0xc1e8 + +0xc1ec
    case TILE_COIN:   return g.bonusChance[TILE_COIN][0] + g.boostLukChance;     // +0xc1d8 + +0xc1dc
    case TILE_SHIELD: return g.bonusChance[TILE_SHIELD][0] + g.boostDexChance;   // +0xc1f0 + +0xc1f4
    default: return 0;                                    // sword: no bonus roll in MatchTile
    }
}

} // namespace

// Tile::GetDamageType @ 0x644bc — how this enemy resolves a chain hit. chainIt is the enemy's own
// node in the chain (the original takes the chain list + an iterator). V34: also used by
// Game::PlayMove for the drag-time Game::UpdateCrossVisibility @ 0x2c5e8 label draws; declared
// in dr/game.h.
int ChainDamageType(Game &g, Tile &t, std::list<Tile *>::iterator chainIt)
{
    if (t.type != TILE_SKULL) return DAMAGE_NONE;                      // +0xa0
    if (t.enemyType == ENEMY_UNKILLABLE && t.countdown != 0)           // +0x25c / +0x260
        return DAMAGE_NONE;
    if (g.turn < 5 && g.ActiveClass().HaveFlaw(FLAW_CONFUSED))         // +0xc: confused early game
        return DAMAGE_BLOCKED;
    auto next = std::next(chainIt);
    bool hasNext = next != g.chain.end();
    if (t.enemyType == static_cast<EnemyType>(0x1b)) {
        // struck only if the next chain tile sits directly above it
        return (hasNext && (*next)->row < t.row) ? DAMAGE_NORMAL : DAMAGE_BLOCKED;
    }
    // dwarf racial trait (race 2, TRAIT_UPPERCUT): enemy vertically sandwiched in its own column
    if (g.Race() == RACE_DWARF && hasNext && (*next)->col == t.col && (*next)->row < t.row
        && chainIt != g.chain.begin()) {
        Tile *prev = *std::prev(chainIt);
        if (prev->col == t.col && t.row < prev->row) return DAMAGE_UPPERCUT;
    }
    if (t.enemyType == ENEMY_MORAL_RIVAL && g.ActiveClass().MortalEnemy() == ENEMY_MORAL_RIVAL)
        return DAMAGE_BLOCKED;                                          // cannot hurt your rival
    // a living meat shield (0x1e) soaks hits meant for every other enemy
    if (t.enemyType == static_cast<EnemyType>(0x1e) || g.GetEnemyCount(static_cast<EnemyType>(0x1e)) < 1)
        return DAMAGE_NORMAL;
    return DAMAGE_BLOCKED;
}

// ---------------------------------------------------------------------------
// Game::MatchTiles @ 0x31bcc — run the drawn chain end to end, then gravity.
// ---------------------------------------------------------------------------
void Game::MatchTiles()
{
    // reset per-chain state (CHAIN.md order 1)
    // +0x5f74 = 0 (unnamed flag; InitNewGame sets it to 1 — not modelled)
    for (int i = 0; i < TILE_TYPE_COUNT; ++i) clearedThisChain[i] = 0;   // +0xc36c..+0xc37c
    explosionSoundPlayed = false;                       // +0x142f4
    poisonSoundPlayed = false;                          // +0x142f5

    int chainLen = static_cast<int>(chain.size());
    // Flaw 2 "Cocky": chains of <= 3 clear but pay nothing (HaveFlaw consulted only when <= 3)
    bool cockyShortChain = chainLen <= 3 && HaveFlaw(FLAW_COCKY);

    std::set<int> cellKeys;      // local std::set<int> built while walking the chain
    int crossings = 0;           // local_88: chain self-crossings, CONFIRMED in MatchTiles @0x31bcc
                                 // :211-242 — key = min(col,prevCol)+min(row,prevRow)*6 is looked up
                                 // in the local std::set (local_50/aiStack_38): found -> local_88++
                                 // else insert_unique; prev is stored at :365-366. CHAIN.md's
                                 // "distinct-cell count" is wrong (the set tracks the keys, the
                                 // counter tracks REPEATS). It is game state: achievement 0x4b at
                                 // :120 (`5 < local_88`) and the pretzel score +0x15e68/+0x15e64/
                                 // +0x15e60 and labels 0x27/0x28/0x29 at :125-155.
    bool allCoins = true;        // bVar2: every chain tile is type 2 -> achievement 0x45 @ len 36
    bool onBorder = true;        // bVar25: chain never enters the board interior -> 0x44 @ len 20
    int prevCol = 0, prevRow = 0; // unaff_r11 / unaff_r10: previous chain tile (loop-carried)

    for (auto it = chain.begin(); it != chain.end(); ++it) {
        Tile &t = **it;                              // node +8
        int col = t.col, row = t.row;                // +0x78 / +0x7c

        // distinct-cell bookkeeping; skipped for the first node (head == current in the decomp)
        if (it != chain.begin() && col != prevCol && row != prevRow) {
            int key = std::min(col, prevCol) + std::min(row, prevRow) * 6;
            if (!cellKeys.insert(key).second) ++crossings;   // same square twice = an X-crossing
        }
        // achievement 0x44 shape test: no chain tile strictly inside the board
        if (onBorder && col != 0 && col != BOARD_SIZE - 1 && row != 0)
            onBorder = (row == BOARD_SIZE - 1);
        prevCol = col; prevRow = row;

        // +0xb794 set: tiles the draw path already handled are skipped (decomp also clears allCoins)
        if (std::find(handledChainTiles.begin(), handledChainTiles.end(), &t)
            != handledChainTiles.end()) {
            allCoins = false;
            continue;
        }
        if (allCoins && t.type != TILE_COIN) allCoins = false;   // bVar2 test: type must be 2

        bool chainImmune = (t.enemyType == ENEMY_UNKILLABLE && t.countdown != 0);
        if (t.type == TILE_SKULL && !chainImmune) {
            // ---- enemy struck by the chain's sword power (+0xc23c, precomputed at release) ----
            int dmg = chainAttack;
            int dt = ChainDamageType(*this, t, it);   // 3 uppercut, 2 full, 1/0 whiff
            bool perfect = false;
            if (dt < 0) {
                // CONFIRMED unreachable: Tile::GetDamageType @0x644bc has only literal returns
                // 0/1/2/3 (lines 16,26,34,46,52,57,60), so dt < 0 can never happen. MatchTiles
                // @0x31bcc:274-277 still keeps iVar19 (the chain attack) and bVar3=false there —
                // i.e. full damage, no "perfect" bonus — which is exactly what this empty branch
                // leaves the rebuild doing (dmg unchanged, perfect=false).
            } else if (dt < DAMAGE_NORMAL) {
                dmg = 0;                              // whiff: poison/crits still applied later
            } else if (dt == DAMAGE_UPPERCUT) {
                perfect = true;
                dmg += swordDamage;            // +0xc17c
            }
            if (AttackSkull(t, dmg, /*fromChain=*/true)) {
                if (perfect)
                    fx().QueueAchievement(ACH_POW_KISSER, 2.0f);   // 0x30 @ 2.0f
                // GUESS: the original's magic-sword branch (achievement 0x38 = 56 "sniper" at
                // 100.0f: previous-but-one chain tile is a sword/arrow (+0x241) more than one
                // cell away) only runs for tiles taken from the handled set; that draw state is
                // not modelled, so it is left out here.
            }
        } else {
            // ---- resource tile (and chain-immune 0x20 enemies) ----
            if (t.specialVariant == SPECIAL_TILE_CHAIN && chainLen == 36)   // +0xa4 == 0
                fx().QueueAchievement(ACH_NECKLACE, 100.0f);        // 0x3b @ 100.0f
            MatchTile(t, cockyShortChain ? MATCH_NO_REWARD : MATCH_USE_OWN);
        }
    }

    // ---- epilogue (CHAIN.md order 4-6) -------------------------------------------------
    // meat shields that were not part of the chain are struck for the chain power
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) {
            Tile &cell = At(c, r);
            if (cell.type != TILE_SKULL || cell.enemyType != static_cast<EnemyType>(0x1e))
                continue;
            if (std::find(chain.begin(), chain.end(), &cell) == chain.end())
                AttackSkull(cell, chainAttack, /*fromChain=*/false);
        }

    if (crossings > 5) fx().QueueAchievement(ACH_PRETZELS, 100.0f);        // 0x4b, 5 < local_88

    if (challenge && mode == MODE_PRETZEL) {           // +0x14335 && +0x14330 == 1
        if (collectedThisTurn > 0) {                   // +0x15e30
            if (longestChain < crossings) longestChain = crossings;   // +0x15e68
            chainAvgX10 = chainAvgX10 == 0                                  // +0x15e64
                ? crossings * 10
                : (crossings * 10 + chainAvgX10 * chainsMade) / (chainsMade + 1);
            ++chainsMade;                              // +0x15e60
            // (the original also initialises a function-local static used by the label colours)
            if (crossings >= 15) {
                fx().QueueAchievement(ACH_PRETZEL_LEGEND, 100.0f);     // 0x4d
                fx().AddLabel(0x29, nullptr, 0);       // orig AddSimpleLabel(SimpleLabelType, xy, ..)
            } else if (crossings >= 10) {
                fx().AddLabel(0x28, nullptr, 0);
            } else if (crossings > 4) {
                fx().AddLabel(0x27, nullptr, 0);
            }
        }
        unknown15e2c += collectedThisTurn;             // +0x15e2c
        collectedThisTurn = 0;                         // +0x15e30
        collectedScratch = 0;                          // +0x15e34
    }

    if (onBorder && chainLen == 20)
        fx().QueueAchievement(ACH_AROUND_THE_WORLD, 100.0f);   // 0x44
    if (allCoins && chainLen == 36)
        fx().QueueAchievement(ACH_GOLD_DIGGER, 100.0f);        // 0x45

    // clear the per-tile "spawned this cascade" byte (+0x8a) on all 36 tiles (D1: not in-chain)
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r)
            At(c, r).inChain = false;

    MoveMatchedTiles();                                // gravity + refill (board.cpp @ 0x2fd7c)

    DrainTokens();                                     // V23: tokens land after the cascade
}

// V23: land queued tokens in spawn order. Game::Process @ 0x339ac runs the token list (+0xb8d4):
// Token::Process, and on landing (token +0x72 == 0, +0x9c == 0) AddResource(+0x78, +0x7c, +0x98);
// it then RETURNS from the whole game block if a menu opened (+0x5ae4/+0x5c94) or hp (+0xc158)
// reached 0 — so later tokens land only after the menu closes. Landing after the cascade + refill
// is what makes AddResource see the post-chain board (seed 33 m52: a corrosive enemy killed by
// the chain is already gone when the shield token lands, so the shield fills armour instead of
// paying upgrade points — AddResource @ 0x32210 TILE_SHIELD branch reads GetEnemyCount(12)).
void Game::DrainTokens()
{
    while (!tokenQueue.empty() && !menuPending && !gameOver) {
        // K12: while a flame/spiky label is pending only tokens whose flight (Token::Process
        // @0x65200) ends on or before the label's landing frame (Game::Process @0x339ac:394/:459,
        // armed at release ProcessInput @0x330c0:265-284) may land — the token loop runs BEFORE
        // the label block in the same frame, so a tie lands the token. Everything still flying
        // waits: if the label kills, Process returns at :176 (hp == 0) and they never land.
        auto it = tokenQueue.begin();
        if (tokenLabelFrame > 0)
            it = std::find_if(tokenQueue.begin(), tokenQueue.end(),
                              [this](const QueuedToken &tk) { return tk.flight <= tokenLabelFrame; });
        if (it == tokenQueue.end()) break;
        QueuedToken tk = *it;
        tokenQueue.erase(it);
        AddResource(static_cast<TileType>(tk.type), tk.tokenType, tk.value);
    }
}

// ---------------------------------------------------------------------------
// Game::MatchTile @ 0x2ac40 — clear one tile and pay its reward.
// ---------------------------------------------------------------------------
void Game::MatchTile(Tile &t, TileType as)
{
    if (t.matched) return;                             // +0x88 guard: cleared once per move
    int kind = (as == MATCH_USE_OWN) ? static_cast<int>(t.type) : static_cast<int>(as);
    t.matched = true;                                  // +0x88 = 1
    fx().TileMatched(t);   // M9 event hook (task A3), presentation only — MatchTile @ 0x2ac40
    if (kind < TILE_TYPE_COUNT) ++clearedThisChain[kind];   // +0xc36c+4*kind (kind 7 is not counted)

    bool skullPath;                                    // decomp LAB_0002acb0: particles + blast
    if (t.type == TILE_SKULL && t.enemyType == ENEMY_KAMIKAZE) {
        skullPath = true;
    } else {
        if (t.broken) {                                // +0x26c broken shield/sword
            fx().QueueAchievement(ACH_PACK_RAT, 1.0f); // 0x49 @ 1.0f
            return;
        }
        if (kind == static_cast<int>(MATCH_NO_REWARD) || kind == TILE_SWORD) return;
        if (t.specialVariant != SPECIAL_TILE_NONE) return;   // +0xa4 != 3: specials opt out
        skullPath = (t.type == TILE_SKULL);
    }

    if (skullPath) {
        fx().AddParticles(&t, 0);                      // AddParticles(this, t, 0)
        if (t.enemyType == ENEMY_KAMIKAZE) {           // 10: 3x3 blast around itself
            if (!explosionSoundPlayed) {               // +0x142f4 once per chain
                fx().QueueSound(SOUND_EXPLOSION);      // 0x1c
                explosionSoundPlayed = true;
            }
            Blast3x3(*this, t, /*skipCentre=*/true);
        }
        if (kind == static_cast<int>(MATCH_NO_REWARD)) return;   // Cocky: clear only, no pay
    }
    TileType type = t.type;                            // reloaded after the blast

    if (type == TILE_POTION) {
        if (t.explosivePotion) {                       // +0x244 rune potion: its own 3x3, no token
            if (!explosionSoundPlayed) {
                fx().QueueSound(SOUND_EXPLOSION);
                explosionSoundPlayed = true;
            }
            Blast3x3(*this, t, /*skipCentre=*/false);  // decomp blasts the centre too
            return;
        }
        // MatchTile @ 0x2ac40:177 — AddToken(this, tile, param_3, 0, 0): the token type is the
        // `as` argument, NOT the tile's own type. Normal chains pass as=own type (heal); Elixir
        // (Skill::Activate @ 0x60210 case 9 -> GrabTiles(0,1) @ 0x30e40) passes as=1, so the
        // potions land as SKULL/xp tokens (measured seed 4 m35: 9 t1 tokens, xp 74->92).
        AddToken(t, static_cast<TileType>(kind), /*bonus=*/false, nullptr);
        if (t.explosivePotion || t.manaPotion || t.acidPotion) return;   // +0x244/242/243: no roll
    } else if (type == TILE_SKULL && t.enemyType != ENEMY_NORMAL) {
        // ---- killed enemy: loot, trophy countdown, achievements (CHAIN.md line 70-73) ----
        std::list<TileType> rewards;
        ChainGetRewards(t, rewards);                   // Tile::GetRewards @ 0x646e4
        for (TileType reward : rewards)
            // MatchTile @0x2ac40 (ARM @0x1aee6): r1 = this+0x5fcc = **board tile (0,0)** (built as
            // r7+0x5f00 then +0xcc) and the pos arg is r5+0x54 = the KILLED tile's screen pos;
            // InitialiseFromTile @0x653d0:36-40 does CopyFrom(tile) then overwrites +0x54/+0x58
            // from pos, so the token's flight starts at the killed tile (K12: the spawn position
            // now drives Token::Process flight frames, so the killed tile is passed as posTile).
            // The tile argument is game state, though: @0x653d0:44-60 it reads the
            // tile's own type/+0x242/+0x243 for the token mode (mana -> GetSpecialTarget draws a
            // skill index, -1 kills the token so no reward lands; acid -> mode 2), and AddToken
            // reads its +0x26c (broken) for the shield-collect counters (@0x2a934:70-80).
            AddToken(board[0][0], reward, /*bonus=*/false, &t);

        if (difficulty != DIFF_EASY) {                 // +0x14328 > 0
            if (!challenge || mode != MODE_SPRINT) {
                Rng0Trace("countdown-gate", 0);        // short-circuit: NO stream-0 draw in normal mode
                --specialSpawnCountdown;               // +0x14da0
            } else if (RandI(0, 100, RAND_COSMETIC) < 50) {   // RandI(0,100,0) @ 0x2ac40:235
                Rng0Trace("countdown-gate", 1);
                --specialSpawnCountdown;
            }
            killFeedsTrophy = true;                    // +0x14d9d = 1 (set on EVERY enemy kill at
                                                       //   difficulty > 0; no reader anywhere in
                                                       //   analysis/decomp -> purpose UNKNOWN)
            if (specialSpawnCountdown < 1) {
                if (specialEnemiesSpawned < 3) {       // +0x14da4 < 3
                    // MatchTile @0x2ac40:241-248 — class gate: the active class's type byte
                    // (+0x14db0) == 0 (Adventurer) or its level byte (+0x14e00) > 9 (level 10)
                    // -> scan the nine class-1..9 level bytes at +0x14f84 (stride 0x184, nine
                    // iterations to 0xda4): spawn only while one of them is 0 (locked). If none
                    // is locked -> :248 countdown pinned to 0 (trophies stop for the rest of the
                    // process; NO RNG draw). A real class at level <= 9 spawns directly.
                    bool spawn;
                    const CharacterClass &active = ActiveClass();
                    if (active.klass == CLASS_ADVENTURER || active.level > 9) {
                        spawn = false;                 // :243-246 scan classes 1..9
                        for (int i = 1; i < CLASS_COUNT; ++i)
                            if (classes[i]->level == 0) { spawn = true; break; }
                        if (!spawn) specialSpawnCountdown = 0;   // :248 pin, no RNG
                    } else {
                        spawn = true;                  // :250 LAB_0002b23a
                    }
                    if (spawn) {
                        if (t.promoted)                    // +0x26d :252-253
                            fx().QueueAchievement(ACH_SAFARI, 100.0f);   // 0x42
                        specialSpawnCountdown = RandI(20, 30, RAND_COSMETIC);   // RandI(0x14,0x1e,0)
                        Rng0Trace("countdown-reset", 1);
                        ++specialEnemiesSpawned;
                        t.SetType(TILE_SPECIAL, 0, static_cast<Difficulty>(0));  // SetSpecialType
                        // (TROPHY) @ 0x65004:7 calls SetType(this,6,0,0) -> 1 stream-0 draw (0x64ef8:23)
                        t.specialVariant = SPECIAL_TILE_TROPHY;   // +0xa4 = 1 (quad is render)
                        t.matched = false;                 // +0x88 = 0: the tile survives as a trophy
                    }
                } else {
                    specialSpawnCountdown = 0;             // :263
                }
            }
        }
        fx().QueueAchievement(ACH_BLOODLUST, 1.0f);            // 0x2e @ 1.0f
        if (t.enemyType == ENEMY_MORAL_RIVAL && ActiveClass().MortalEnemy() == ENEMY_MORAL_RIVAL)
            fx().QueueAchievement(ACH_CANT_TOUCH_THIS, 100.0f); // 0x2a (0xe = your rival)
        if (t.enemyType == ENEMY_GOLDEN)
            fx().QueueAchievement(ACH_JACKPOT, 100.0f);         // 0x2c
        if (Race() == RACE_CHANGELING) {                        // race 9
            fx().QueueAchievement(ACH_TEARS_IN_RAIN, 2.0f);     // 0x37
            Shapeshift();                                       // @ 0x14f79
        }
        fx().ShowHelp(22, &t);      // HelpMenu::Initialise(..., 0.085f, 0.085f, 0x16)
        return;
    } else {
        AddToken(t, static_cast<TileType>(kind), /*bonus=*/false, nullptr);
        // shield -> armour/upgrade pts, coin -> gold, sword -> attack, potion -> heal (resources.cpp)
    }

    // ---- bonus roll: from the 4th cleared tile of this kind onward (CHAIN.md line 64-69) ----
    if (kind >= TILE_TYPE_COUNT || clearedThisChain[kind] < 4) return;
    int chance = BonusChanceFor(*this, kind);
    if (clearedThisChain[kind] > 9 && HavePerk(PERK_DARING))
        chance = 100;                                  // perk 2 "Daring" forces the roll
    if (chance < RandI(1, 100, RAND_OTHER)) return;    // RandI(1,100,5)
    fx().ShowHelp(1, &t);          // HelpMenu::Initialise(..., 0.04f, scale, id 1) popup
    AddToken(t, static_cast<TileType>(kind), /*bonus=*/true, nullptr);   // bonus token
    fx().AddLabel(0, &t, 0);       // AddSimpleLabel(Tile*,TileType) @ 0x28744:33 passes type 0 to
                                   // SimpleLabel::Initialise (V24: type drives the stream-0 draw)
}

// ---------------------------------------------------------------------------
// Game::AttackSkull @ 0x2f630 — one hit on an enemy tile; true when it dies.
// ---------------------------------------------------------------------------
bool Game::AttackSkull(Tile &t, int damage, bool fromChain)
{
    if (getenv("DR_VAMP") && t.type == TILE_SKULL && t.enemyType == ENEMY_VAMPIRE)
        std::fprintf(stderr, "  [AS] vamp hp=%d def=%d dmg=%d fromChain=%d\n",
                     t.health, t.defence, damage, (int)fromChain);
    // chain-immune 0x20 enemy with its armour level up: the hit is ignored entirely
    if (t.type == TILE_SKULL && t.enemyType == ENEMY_UNKILLABLE && t.countdown != 0) return false;

    if (fromChain && stealTokenMode)                   // +0xc383, skill 0 "Steal" (SKILLS.md)
        AddToken(t, TILE_COIN, /*bonus=*/false, nullptr);   // a coin per monster attacked

    int armour = t.defence;                            // +0xac
    int newHp = t.health - std::max(0, damage - armour);     // +0xb0
    if (newHp < 1) {
        if (t.enemyType != ENEMY_UNKILLABLE) {
            ++enemiesKilled;                           // +0x15e14
            ++killsCounted;                            // +0x14d90
            killedAttackSum += t.GetAttack();          // +0x15cf4 (Tile::GetAttack @ 0x62934)
            MatchTile(t, MATCH_USE_OWN);               // decomp passes 5 -> loot + achievements
            return true;
        }
        // the 0x20 enemy only dies once: it is stripped instead of killed
        t.countdown = 1;                               // +0x260 = 1
        UpdateEnemyUVDraw(t);                          // AttackSkull @ 0x2f630:34 — stream-0 glow draw
        t.resurrectBlocked = true;                     // +0x26e = 1
        t.attack = 0;                                  // +0xa8
        t.defence = 0;                                 // +0xac
        t.health = 1;                                  // +0xb0
        t.poisonTurns = 0;                             // +0x248
        t.poison = 0;                                  // +0x24c
        t.poisonAddedThisTurn = 0;                     // +0x250
        fx().AddFlare(&t, FLARE_YELLOW);               // AddFlare(1, xy, 0xff,0xff,0x00,0xff)
        // +0xc150/+0xc154 = shake/spawn timer floats — animation, not modelled
        return false;
    }
    if (newHp != t.health) {                           // damaged, but still alive
        fx().AddParticles(&t, 2);                      // AddParticles(this, t, 2)
        t.health = newHp;
        // Tile::UpdateText — rendering
    }
    if (armour <= damage) damage = armour;             // cap the armour-chip rolls at the armour
    if (damage > 0) {
        int crits = 0;                                 // each point rolled for a armour chip
        for (int i = 0; i < damage; ++i)
            if (RandI(0, 100, RAND_OTHER) < critChance) ++crits;   // +0xc18c, RandI(0,100,5)
        if (crits > 0) {
            fx().AddParticles(&t, 1);                  // AddParticles(this, t, 1)
            t.defence -= crits;                        // chip the armour
            // Tile::UpdateText — rendering
        }
    }
    if (fromChain) {
        if (poisonDamage > 0 && t.enemyType != ENEMY_POISONOUS) {   // +0xc21c, enemy 9 immune
            if (!poisonSoundPlayed) {                  // +0x142f5 once per chain
                fx().QueueSound(SOUND_POISON);         // 0x1d
                poisonSoundPlayed = true;
            }
            // Tile::AddPoison @ 0x62ad4 (no tile.h decl): the first application also arms the
            // poison-start UI bytes +0x254/+0x258/+0x255, which are not modelled
            t.poisonTurns += poisonDamage;             // +0x248
            t.poison += poisonDamage;                  // +0x24c
            t.poisonAddedThisTurn += poisonDamage;     // +0x250
            fx().AddLabel(6, &t, 0);                   // AddSimpleLabel(6, xy, 0, 0xf, colour)
        }
        if (Race() == RACE_ORC && RandI(1, 100, RAND_OTHER) < 11) {   // race 5, 10% (stream 5)
            t.frozen = true;                           // Tile::SetFreezeVisibility(true) @ 0x52e95
            fx().QueueAchievement(ACH_HAMMER_TIME, 2.0f);   // 0x33 @ 2.0f
            return false;
        }
    }
    return false;
}

} // namespace dr
