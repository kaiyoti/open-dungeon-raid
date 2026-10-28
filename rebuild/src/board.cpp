// Board system: NewGame/InitNewGame, RandomiseTile, SwapTiles, MoveMatchedTiles (gravity+refill),
// GetTileCount, GetEnemyCount. Source: analysis/board/BASICS.md (+ decomp for exact numbers).
// UI/render calls (labels, quads, interface bars, screen shake) are not rebuilt.
#include "dr/game.h"
#include "dr/character_class.h"
#include "dr/item.h"
#include "dr/skill.h"
#include "dr/rng.h"
#include <ctime>
#include <vector>

namespace dr {

// Game::NewGame @ 0x2de68 — clears the challenge flag, runs InitNewGame, then UI screens (not rebuilt).
void Game::NewGame(Difficulty d)
{
    difficulty = d;        // CONFIRMED: NewGame @0x2de68 takes no args; +0x14328 is set elsewhere
                           //   (Game ctor @0x28c8c:107, StartTutorial @0x2ddb0:12, DeserialiseState)
    challenge = false;     // +0x14335 = 0
    InitNewGame();
}

// Game::InitNewGame @ 0x2d7a4 — seed RNG, reset run state, fill the 6x6 board, rebuild items.
void Game::InitNewGame()
{
    SetRandSeed(seedOverride ? seedOverride : static_cast<uint32_t>(std::time(nullptr)), RAND_ALL);   // SetRandSeed(time(0), 6)
    FlushRandSeeds();
    // CalculateTilePositions @ 0x25f64 — UI, not rebuilt.
    // InitNewGame @0x2d7a4:24-27 — UnlockAchievement for ALL 10 classes at every game start (the
    // original re-queues the class/race/master achievements for every class with level > 0 plus
    // the all-maxed check; Achievement hooks are presentation only, no game state).
    for (int i = 0; i < CLASS_COUNT; ++i) classes[static_cast<std::size_t>(i)]->UnlockAchievement();

    turn = 0;                    // +0xc
    chainAttack = 0;             // +0xc23c
    specialEnemiesSpawned = 0;   // +0x14da4
    enemiesKilled = 0;           // +0x15e14
    poisonLevel = 0;             // +0x14d88
    pendingHeal = 0;               // +0xc6e0 (InitNewGame @0x2d7a4:46)
    // W9/V26 follow-up: this spot used to hold a comment only ("+0x1469c, +0x14a10, +0x15d1c,
    // +0x15e71, +0x15e2c..+0x15e5c ... = 0") while the modelled counters stayed untouched — the
    // fresh Game object every web run created hid it. With one Game per session (dr_init, W9) a
    // second InitNewGame must clear them exactly like InitNewGame @0x2d7a4 does (:27-43, :47).
    // NOT modelled in game.h, so nothing to assign: +0x14a10, +0x15d1c, +0x15e38, +0x15e48,
    // +0x15e4c, +0x15e5c.
    survivedSprint = false;        // +0x15e71 (:27)
    unknown15e2c = 0;              // +0x15e2c (:28)
    collectedThisTurn = 0;         // +0x15e30 (:29)
    collectedScratch = 0;          // +0x15e34 (:30)
    chainsMade = 0; chainAvgX10 = 0; longestChain = 0;   // +0x15e60/64/68 (:31-33)
    killsCollected = coinsCollected = shieldsCollected = 0;             // +0x15e3c/40/44 (:36/38/40)
    killsCollectedBonus = coinsCollectedBonus = shieldsCollectedBonus = 0;  // +0x15e50/54/58 (:37/39/41)
    firePool = 0;                 // +0x1469c (:47)
    promotionEnabled = true;     // +0x1433c = 1
    promoteCountdown = (!challenge || mode != MODE_SPRINT)
                           ? (HaveFlaw(FLAW_ZEALOUS) ? 49 : 50)          // +0x14338
                           : 200;                                        // sprint
    // Per-turn / skill-activation bytes that InitNewGame writes but that are NOT in the save, so a
    // rebuilt game that is initialised a second time must clear them exactly like the original.
    // V26: they were only named in a comment ("… = 0") above and never assigned — the skill
    // differential (tools/emu/unit_skills.py) caught it (a carried doubleShields/doubleCoins
    // doubled every later collect). Sources: InitNewGame @0x2d7a4:70-98 and :276.
    hackCombo = slashCombo = false;                    // +0x14da9 / +0x14daa (:70-71)
    noForcedCoin = false;                             // +0x14dab (:72)
    runeActive = false; carryRune = 0;                // +0x15cd9 / +0x15cda (:73-74)
    reviveUsed = false; carryRevive = 0;              // +0x15cec / +0x15ced (:75-76)
    reviveAvailable = false;                          // +0x15cee (:77)
    berserkActive = false;                            // +0x15cef (:78)
    firstBoard = true;           // +0xc380 = 1 (set before the fill loop, cleared after) (:82)
    counterAttackPending = false;// +0xc382 (:84)
    stealTokenMode = false; carrySteal = 0;           // +0xc383 / +0xc384 (:85-86)
    oneHitSkulls = false;        // +0x1433d (:87)
    forceCoinMode = false;       // +0x1433e (:88)
    carryOneHitSkulls = 0; carryForceCoin = 0;        // +0x1433f / +0x14340 (:89-90)
    doubleDamage = false;                             // +0xc234 (:91)
    doubleShields = doublePotions = doubleCoins = false;   // +0xc235/6/7 (:92-94)
    carryDoubleDamage = carryDoubleArmour = 0;        // +0xc238 / +0xc239 (:95-96)
    carryDoubleHealth = carryDoubleGold = 0;          // +0xc23a / +0xc23b (:97-98)
    carryBerserk = 0;                                 // +0x15cf0 (:94)
    turnSkillMarker = false;                          // +0x14d94 (:276)
    for (int row = 0; row < BOARD_SIZE; ++row)
        for (int col = 0; col < BOARD_SIZE; ++col) {
            PositionTile(At(col, row), col, row, false);
            RandomiseTile(At(col, row));
        }
    firstBoard = false;          // +0xc380 = 0
    firstBoardAvoidSword = false;// +0xc381 = 0
    // +0xc1a8/0xc1b0/0xc1b8/0xc1c0 = 1 (InitNewGame @0x2d7a4:122-123): the four stat-skill
    // counts — SetSkill @0x2be04:127,130 increments them, StatsMenu::Show @0x614bc:74 displays
    // them, SerialiseState @0x265f4:80-81 saves them (was "saved resource counters", wrong).
    for (int &u : unknownC1A8) u = 1;   // +0xc1a8/0xc1b0/0xc1b8/0xc1c0 = 1
    baseDamage = 3;              // +0xc180
    if (HavePerk(PERK_STRONG)) baseDamage += 1;
    baseMaxHp = 40;              // +0xc15c = 0x28
    if (challenge && mode == MODE_PRETZEL) baseMaxHp = 100;
    if (HavePerk(PERK_TOUGH))  baseMaxHp += 10;
    if (HaveFlaw(FLAW_FRAIL))  baseMaxHp -= 10;
    hp = baseMaxHp;              // +0xc158 = +0xc15c
    xp = 0; upgradePoints = 0; gold = 0;                 // +0xc170 / +0xc174 / +0xc178
    healBase = 1; armourBase = 1;                        // +0xc1c8 / +0xc1d0
    bonusChance[TILE_COIN][0]  = 20; bonusChance[TILE_POTION][0] = 20;    // +0xc1d8 / +0xc1e0
    bonusChance[TILE_SKULL][0] = 20; bonusChance[TILE_SHIELD][0] = 20;    // +0xc1e8 / +0xc1f0
    if (HavePerk(PERK_FRUGAL))   bonusChance[TILE_COIN][0]   += 10;       // +0xc1d8 (MatchTile case 2 = coin)
    if (HaveFlaw(FLAW_RECKLESS)) bonusChance[TILE_SHIELD][0] -= 10;       // +0xc1f0 (case 3 = shield)
    armour = 0; maxArmour = 0;   // +0xc164 / +0xc168

    items.clear();               // original deletes the 5 Item* at +0xc248..+0xc258 ...
    for (int i = 0; i < ITEM_TYPE_COUNT; ++i) {
        auto it = std::make_unique<Item>();
        it->game = this;
        it->Initialise(static_cast<ItemType>(i), nullptr);   // Item::Item(this, game, type) @ 0x51...
        items.push_back(std::move(it));
    }
    InitBaseItems();             // +0xc248..0xc258 GetItem templates (separate from equipped, V8)
    // original also resets each of the 4 skills' upgrade slots to 0xff, then sets skill count
    // (+0xc368) to 0 — the slots are not modelled in Skill, so clearing the list is the effect:
    skills.clear();
    bonusMaxHp = 0;              // +0xc160
    hasWeapon = false;           // +0xc224
    UpdateStats();               // resources.cpp (not rebuilt yet)
    xpFrac = 0.0f; goldFrac = 0.0f; upFrac = 0.0f;         // +0xc190/0xc198/0xc1a0 (DAT_0002dcd0 = 0)
    shieldShatterActive = false; // +0xc350 = 0
    // +0xbc5c attack-sequence state = 0; +0x5f74 = 1
    fallingTiles.clear();        // +0xb7ac
    tokenQueue.clear();          // +0xb8e0/0xb8f0/0xb900 token vectors = 0 (InitNewGame @0x2d7a4:269-271;
                                 //   was comment-only until W9 — a second run on the same Game kept the
                                 //   in-flight tokens, V26-style)
    level = 1;                   // +0xc20c = 1
    xpHundreds = 0; goldHundreds = 0; upHundreds = 0;      // +0xc210/0xc214/0xc218
    // +0xc388, +0x14344, +0x146b8, +0x14a2c = 0 (InitNewGame @0x2d7a4:277-280) — no isolated
    // reader (PROVENANCE.md); +0x14d94 turnSkillMarker is cleared with the block at :276 above.
}

// Game::PositionTile @ 0x24560 — screen placement. Headless: only the falling flag matters.
void Game::PositionTile(Tile &t, int col, int row, bool animate)
{
    if (animate) { t.falling = true; return; }   // +0x94 = 1, fall target = cell (col,row)
    // row < 0: start above the screen, y = yOffset + 0.246875 + cell*(row+0.5), x = 0.003125 + cell*(col+0.5);
    // row >= 0: xy from the precomputed position table (+0xb7b4). Rendering only — not rebuilt.
    (void)col; (void)row;
}

// Game::GetNearestUnmatchedTile @ 0x24510 — row of the nearest unmatched tile at row-1 and above,
// -1 if every tile above is matched. CONFIRMED: the caller tests the sign only
// (MoveMatchedTiles @0x2fd7c:62-63 `if (iVar2 < 0)`); the walk stops at -1 (@0x24510).
int Game::GetNearestUnmatchedTile(int col, int row)
{
    int r = row - 1;
    if (r < 0 || !At(col, r).matched) return r;
    while (--r >= 0 && At(col, r).matched) {}
    return r;
}

// Game::IsTimeToPromote @ 0x250b0 — sprint-mode skull promotion / normal-mode promotion countdown.
bool Game::IsTimeToPromote()
{
    if (!challenge || mode != MODE_SPRINT) return promoteCountdown == 0;
    if (promoteCountdown == 0) return true;
    if (promoteCountdown == 200) return false;
    bool hit = HaveFlaw(FLAW_ZEALOUS) ? ((turn - 3) % 9 == 0) : ((turn - 4) % 10 == 0);
    if (hit) promoteCountdown = 0;
    return hit;
}

// Game::RandomiseTile @ 0x2d584 — roll a tile type (RandI(0,4)) and apply spawn-time perks/flaws.
void Game::RandomiseTile(Tile &t)
{
    int type;
    if (!forceCoinMode || firstBoard || noForcedCoin)
        type = RandI(0, 4, RAND_TILE);
    else
        type = TILE_COIN;        // +0x1433e set forces type 2 (coin) — BASICS.md review note
    if (challenge && mode == MODE_SPRINT && IsTimeToPromote())
        type = TILE_SKULL;       // promote mode forces type 1
    if (firstBoard) {            // first board: re-roll unwanted types (flag +0xc381 picks which)
        if (!firstBoardAvoidSword) { while (type == TILE_SKULL) type = RandI(0, 4, RAND_TILE); }
        else                       { while (type == TILE_SWORD) type = RandI(0, 4, RAND_TILE); }
    }
    t.SetType(static_cast<TileType>(type), turn, difficulty);   // level = +0xc, difficulty = +0x14328
    ++tilesSpawned;              // +0x15e20
    if (t.type == TILE_SKULL) ++enemiesSpawned;   // +0x15e24 (reads +0xa0 after SetType)
    switch (type) {
    case TILE_POTION:
        if (HavePerk(PERK_GIFTED) && RandI(1, 100, RAND_TILE) < 6)
            t.manaPotion = true;                 // Tile::MakeManaPotion @ 0x642dc (5%)
        break;
    case TILE_SKULL:
        if (oneHitSkulls) {                      // +0x1433d: RandomiseTile @0x2d584:48-51
            t.health = 1;                        //   tile+0xb0 = 1, then UpdateText (UI)
            if (t.enemyType != ENEMY_NORMAL)
                fx().QueueAchievement(ACH_PREMONITION, 100.0f);   // 0x3e, progress 100
        }
        if (ActiveClass().MortalEnemy() == ENEMY_CORROSIVE && t.enemyType == ENEMY_CORROSIVE) {
            armour = 0;                          // +0xc164 = 0 (shield bar update is UI)
            InialiseShieldShatter();
            fx().QueueSound(SOUND_ARMOUR_BREAK);
        } else if (t.enemyType == ENEMY_FLEEING) {
            JumpDown(t);                         // fleeing enemy spawns by jumping down
        }
        break;
    case TILE_SHIELD:
        if (HaveFlaw(FLAW_CLUMSY) && RandI(1, 100, RAND_TILE) < 4)
            t.broken = true;                     // Tile::MakeBroken @ 0x64a80 (4%)
        break;
    case TILE_SWORD:
        if (HavePerk(PERK_AGILE) && RandI(1, 100, RAND_TILE) < 6)
            t.arrow = true;                      // Tile::MakeArrow @ 0x64294 (5%)
        break;
    default: break;
    }
}

// Game::SwapTiles @ 0x28b14 — swap the two tiles' contents, animating them to each other's cell.
void Game::SwapTiles(int c1, int r1, int c2, int r2)
{
    Tile &t1 = At(c1, r1), &t2 = At(c2, r2);
    PositionTile(t2, c1, r1, false);
    PositionTile(t1, c2, r2, false);
    // original also copies position-table xy into +0x5c/+0x60 and sets the swap-anim byte +0x64 = 1;
    // decomp SwapTiles @0x28b14:24-25,33-34: tile+0x64 = 1 (swap-anim byte) plus the position-table
    // xy copy into +0x5c/+0x60 — presentation; the closest modelled state is the falling flag.
    t2.falling = true; t2.col = c1; t2.row = r1;
    t1.falling = true; t1.col = c2; t1.row = r2;
    std::swap(t1, t2);           // original: temp Tile copy + operator= x2
}

// Game::MoveMatchedTiles @ 0x2fd7c — gravity + refill: per column bottom-to-top, every matched cell
// pulls the nearest unmatched tile above it down; cells with nothing above re-roll "spawning above".
void Game::MoveMatchedTiles()
{
    std::vector<Tile *> stabbed;  // original: std::set<Tile*> of fleeing enemies that fell
    for (int col = 0; col < BOARD_SIZE; ++col) {
        int spawnRow = 0;         // local_2e0: 0, -1, -2, ... rows above the board
        for (int row = BOARD_SIZE - 1; row >= 0; --row) {
            Tile &cell = At(col, row);
            if (!cell.matched) continue;
            int src = GetNearestUnmatchedTile(col, row);
            if (src < 0) {
                --spawnRow;
                PositionTile(cell, col, spawnRow, false);   // start above the board
                PositionTile(cell, col, row, true);         // animate into this cell
                fx().TileFell(col, spawnRow, row, true);    // M9 event hook (task A3) — MoveMatchedTiles @ 0x2fd7c refill
                // decomp: *(float*)(…+0x6068 /* tile+0x6c */) = (float)(row+1) * DAT_0002ffb0 with
                // DAT_0002ffb0 = 9420.75f (MoveMatchedTiles @0x2fd7c:42,87,99) — semantics UNKNOWN
                // (never read: fallDelay has no reader in the rebuild, presentation only).
                cell.fallDelay = (row + 1) * 0.8f;
                fallingTiles.push_back(&cell);
                cell.matched = false;                       // +0x88 = 0
                cell.inChain = true;                        // +0x8a = 1: new tiles do not attack this turn (BASICS.md)
                RandomiseTile(cell);
                continue;
            }
            Tile &srcTile = At(col, src);
            PositionTile(srcTile, col, row, true);          // animate the source tile down
            fx().TileFell(col, src, row, false);            // M9 event hook (task A3) — MoveMatchedTiles @ 0x2fd7c shift
            srcTile.col = col; srcTile.row = row;           // +0x78/+0x7c
            srcTile.fallDelay = (row + 1) * 0.8f;           // +0x6c
            cell.col = col; cell.row = src;                 // matched tile takes the source's grid row
            std::swap(srcTile, cell);                       // temp Tile copy + operator= x2
            fallingTiles.push_back(&cell);
            if (cell.type == TILE_SKULL && cell.enemyType == ENEMY_FLEEING)
                stabbed.push_back(&cell);
        }
    }
    for (Tile *t : stabbed) Stab(*t);
}

// Game::Stab @ 0x2fcd8 — a fleeing enemy that fell stabs the player for its attack (no armour).
void Game::Stab(Tile &t)
{
    fx().QueueSound(SOUND_ATTACK_HIT);          // 0x1b
    fx().QueueSound(SOUND_DAMAGE);              // 0x11
    // K2: AddSimpleLabel(0x13, tile pos, …) @0x2fcd8:16 and AddSimpleLabel(2, poison pos, …)
    // @0x2fcd8:27 each consume 1 stream-0 draw (SimpleLabel::Initialise @0x5fbc4, types draw).
    fx().AddLabel(0x13, &t, 0);
    int dmg = t.GetAttack();
    hp = hp > dmg ? hp - dmg : 0;               // +0xc158 = max(0, hp - attack)
    fx().AddLabel(2, nullptr, dmg);             // damage number at the poison icon, colour 0xb
    CheckEndGame();                             // combat.cpp (not rebuilt yet)
}

// Game::JumpDown @ 0x2d3cc — a tile (fleeing enemy) swaps itself with the bottom cell of its column.
void Game::JumpDown(Tile &t)
{
    if (t.row != BOARD_SIZE - 1) {
        SwapTiles(t.col, BOARD_SIZE - 1, t.col, t.row);
        // K2: not just UI — AddSimpleLabel(this,0x25,…) @0x2d3cc:17 (SimpleLabel::Initialise
        // @0x5fbc4, type 0x25 draws) and AddFlare(this,1,…) @0x2d3cc:26 (AddFlare @0x29894:121)
        // each consume 1 stream-0 draw; only the screen shake +0xc150/+0xc154 is UI.
        fx().AddLabel(0x25, &t, 0);
        fx().AddFlare(&t, 1);
    }
}

// Game::InialiseShieldShatter @ 0x2535c — starts the shield-shatter animation. CONFIRMED no
// gameplay effect: the only readers are Render @0x25924:206, Process @0x339ac:102 and
// ProcessShieldShatter @0x252ac:15 (which clears +0xc350); InitNewGame/this set it.
void Game::InialiseShieldShatter()
{
    shieldShatterActive = true;                 // +0xc350 = 1 (quad alphas/timers are UI)
}

// Game::GetTileCount @ 0x246cc — cells with TileType == t.
int Game::GetTileCount(TileType t) const
{
    int n = 0;
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row)
            if (board[col][row].type == t) ++n;
    return n;
}

// Game::GetEnemyCount @ 0x247b8 — skull cells with EnemyType == e.
int Game::GetEnemyCount(EnemyType e) const
{
    int n = 0;
    for (int col = 0; col < BOARD_SIZE; ++col)
        for (int row = 0; row < BOARD_SIZE; ++row)
            if (board[col][row].type == TILE_SKULL && board[col][row].enemyType == e) ++n;
    return n;
}

} // namespace dr
