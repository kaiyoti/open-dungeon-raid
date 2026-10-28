// Game state + rules. Original: class Game (one huge object). Member comments give the original
// Game+offset (analysis/FIELDS.md). Only gameplay state is modelled; render/UI objects are replaced by
// the Platform hooks (platform.h). Method comments give the original decomp address.
#pragma once
#include <array>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "enums.h"
#include "item.h"
#include "platform.h"
#include "rng.h"
#include "skill.h"
#include "tile.h"

namespace dr {

constexpr int BOARD_SIZE = 6;
constexpr int MAX_SKILLS = 4;

struct Item;            // item.h
struct ItemUpgrade;     // item.h (needed by the items.cpp extras block)
struct Skill;           // skill.h
struct CharacterClass;  // character_class.h

class Game {
public:
    explicit Game(Platform *platform = nullptr);
    ~Game();

    // ---- board (+0x5fcc, 6x6 Tiles) -----------------------------------------------------------
    std::array<std::array<Tile, BOARD_SIZE>, BOARD_SIZE> board;   // board[col][row]
    Tile &At(int col, int row) { return board[col][row]; }
    std::list<Tile *> chain;          // +0xb78c drawn chain, in draw order
    std::list<Tile *> fallingTiles;   // +0xb7ac

    // ---- turn / mode --------------------------------------------------------------------------
    int        turn = 0;              // +0xc (also the "level" passed to Tile::SetType)
    Difficulty difficulty = DIFF_NORMAL;   // +0x14328
    bool       challenge = false;     // +0x14335 non-Classic mode
    GameMode   mode = MODE_CLASSIC;   // +0x14330 (SPRINT / PRETZEL when challenge)
    bool       gameOver = false;      // +0xce64

    // ---- player -------------------------------------------------------------------------------
    int hp = 40;                      // +0xc158
    int baseMaxHp = 40;               // +0xc15c
    int bonusMaxHp = 0;               // +0xc160
    int armour = 0;                   // +0xc164
    int maxArmour = 0;                // +0xc168
    int baseDamage = 3;               // +0xc180 (+1 perk Strong)
    int poisonLevel = 0;              // +0x14d88 damage per turn, cured by potions
    int poisonThisTurn = 0;           // +0x14d84 incoming poison from this turn's attackers
    int incomingDamage = 0;           // +0xc244
    int chainAttack = 0;              // +0xc23c attack power of the current chain
    int swordDamage = 0;              // +0xc17c damage per sword in a chain (weapon stat); also the DAMAGE_UPPERCUT bonus
    int critChance = 0;               // +0xc18c % (cap 95)
    int armourBreakChance = 50;       // +0xc188 % (min 5)
    int spikes = 0;                   // +0xc228
    int blunting = 0;                 // +0xc22c
    int regen = 0;                    // +0xc230 HP per turn
    int poisonDamage = 0;             // +0xc21c poison applied per attack
    int pendingHeal = 0;              // +0xc6e0 queued Life-Leech heal (Process @0x339ac:313-320)

    // ---- resources (analysis/progression/RESOURCES.md) ----------------------------------------
    int level = 0;                    // +0xc20c player level
    int xp = 0, gold = 0, upgradePoints = 0;           // +0xc170 / +0xc178 / +0xc174 (0..99)
    int xpHundreds = 0, goldHundreds = 0, upHundreds = 0;   // +0xc210 / +0xc214 / +0xc218
    float xpFrac = 0, xpRate = 0;     // +0xc190 / +0xc194
    float goldFrac = 0, goldRate = 0; // +0xc198 / +0xc19c
    float upFrac = 0, upRate = 0;     // +0xc1a0 / +0xc1a4
    int healBase = 0, healBonus = 0;  // +0xc1c8 / +0xc1cc heal per potion
    int armourBase = 0, armourBonus = 0;  // +0xc1d0 / +0xc1d4 armour per shield
    bool doubleShields = false, doublePotions = false, doubleCoins = false; // +0xc235/+0xc236/+0xc237
    // bonus-roll chance pairs (MatchTile, 4th+ tile of a type): {base, bonus} per resource type
    int bonusChance[TILE_TYPE_COUNT][2] = {};   // [TileType]{base,bonus}: coin +0xc1d8/dc, potion +0xc1e0/e4, skull +0xc1e8/ec, shield +0xc1f0/f4
    int clearedThisChain[TILE_TYPE_COUNT] = {}; // +0xc36c..

    // ---- character ----------------------------------------------------------------------------
    std::vector<std::unique_ptr<CharacterClass>> classes;  // +0x14dac, 10 x 0x184
    int activeClass = 0;                                   // +0x15cd4
    int unknown15cd8 = 0;                                  // +0x15cd8 set 1 by Game::Start @0x260ac:181
                                                           //   and the class-commit path; UNKNOWN meaning,
                                                           //   no reader found in analysis/decomp
    std::vector<std::unique_ptr<Item>> items;              // +0xc25c.. equipped items (one per ItemType)
    std::vector<std::unique_ptr<Skill>> skills;            // +0xc358, count +0xc368 (<= 4)

    // ---- stats (UpdateStats, END_OF_TURN.md) --------------------------------------------------
    int enemiesKilled = 0;            // +0x15e14
    int specialSpawnCountdown = 0;    // +0x14da0
    int specialEnemiesSpawned = 0;    // +0x14da4
    int chainsMade = 0, chainAvgX10 = 0, longestChain = 0;  // +0x15e60 / +0x15e64 / +0x15e68
    int unknown15e2c = 0;             // +0x15e2c saved, meaning unknown
    int unknownC1A8[4] = {};          // +0xc1a8/+0xc1b0/+0xc1b8/+0xc1c0 saved ints, meaning unknown (InitNewGame sets 1)
    std::string playerName;           // +0xc1f8 (max 16 chars)
    int helpState = 0;                // HelpMenu+0x38 tutorial progress bits (saved)

    // ---- systems (each implemented by one src/*.cpp, see rebuild/README.md) -----------------
    // board.cpp
    void NewGame(Difficulty d);                       // @ 0x2de68 / InitNewGame @ 0x2d7a4
    void RandomiseTile(Tile &t);                      // @ 0x2d584
    void SwapTiles(int c1, int r1, int c2, int r2);   // @ 0x28b14
    void MoveMatchedTiles();                          // @ 0x2fd7c gravity + refill
    int  GetTileCount(TileType t) const;              // @ 0x246cc
    int  GetEnemyCount(EnemyType e) const;            // @ 0x247b8
    // chain.cpp
    void MatchTiles();                                // @ 0x31bcc resolve the released chain
    void MatchTile(Tile &t, TileType as);             // @ 0x2ac40
    bool AttackSkull(Tile &t, int damage, bool fromChain);   // @ 0x2f630, true if killed
    void AddRevengeHealth();                         // @ 0x2d294 body in enemy_abilities.cpp
    // combat.cpp
    void ApplyIncomingDamage();                       // ProcessAttack @ 0x311b8 (states 5/6)
    void EndOfTurn();                                 // @ 0x3289c
    bool CheckEndGame();                              // @ 0x2f960
    void DoCounterattack();                           // @ 0x30f38
    // resources.cpp
    void AddResource(TileType t, int tokenType, int value);  // @ 0x32210
    void UpdateStats();                               // @ 0x26c94
    bool ShopSkillGate(int id) const;   // FoldoutMenu::Initialise @ 0x42794 word test at +0xc358+id*4
    // skills.cpp
    void ActivateSkill(int slot);                     // Skill::Activate @ 0x60210 dispatch
    // save.cpp
    std::vector<uint8_t> SerialiseState() const;      // @ 0x265f4
    bool DeserialiseState(const std::vector<uint8_t> &bytes);  // @ 0x27c8c

    // ==== extras: board.cpp (only the worker owning board.cpp edits this block) ====
    void InitNewGame();                // @ 0x2d7a4 (NewGame's body)
    void PositionTile(Tile &t, int col, int row, bool animate);  // @ 0x24560
    int  GetNearestUnmatchedTile(int col, int row);              // @ 0x24510, -1 = none above (caller tests < 0, MoveMatchedTiles @0x2fd7c:63)
    bool IsTimeToPromote();            // @ 0x250b0
    void JumpDown(Tile &t);            // @ 0x2d3cc
    void InialiseShieldShatter();      // @ 0x2535c (original's spelling)
    void Stab(Tile &t);                // @ 0x2fcd8
    bool firstBoard = false;           // +0xc380 first-board fill flag (gates RandomiseTile re-rolls)
    bool firstBoardAvoidSword = false; // +0xc381 fill companion: 0 = re-roll skulls, 1 = re-roll swords
    bool promotionEnabled = true;      // +0x1433c
    int  promoteCountdown = 50;        // +0x14338 time-to-promotion (50/49 normal, 200 sprint)
    bool forceCoinMode = false;        // +0x1433e forces RandomiseTile to coin (RandomiseTile @0x2d584:11-16)
    bool noForcedCoin = false;         // +0x14dab disables that; set by CollectBoard @0x30a2c:20 (Wish)
    bool oneHitSkulls = false;         // +0x1433d spawned skulls get health 1 (RandomiseTile @0x2d584:48-51)
    bool counterAttackPending = false; // +0xc382
    int  tilesSpawned = 0;             // +0x15e20 RandomiseTile counter
    int  enemiesSpawned = 0;           // +0x15e24 skulls spawned counter
    bool shieldShatterActive = false;  // +0xc350
    bool hasWeapon = false;            // +0xc224
    // ==== end extras: board.cpp ====
    // ==== extras: chain.cpp (only the worker owning chain.cpp edits this block) ====
    // Callees owned by other systems, declared here so chain.cpp compiles (needs listed in
    // tasks/R2/RESULT.md — if another block declares the same method, keep only one).
    void AddToken(Tile &t, TileType type, bool bonus, const Tile *posTile);   // @ 0x2a934 (resources)
    void Shapeshift();                                 // @ 0x14f79 class-9 race ability
    std::vector<Tile *> handledChainTiles; // +0xb794 orig std::set<Tile*> tiles the draw path already handled
    bool explosionSoundPlayed = false;     // +0x142f4 sound-once flag (3x3 blast)
    bool poisonSoundPlayed = false;        // +0x142f5 sound-once flag (chain poison)
    bool stealTokenMode = false;           // +0xc383 skill 0 "Steal": coin per monster attacked from a chain
    int  killsCounted = 0;                 // +0x14d90 second kill counter
    int  killedAttackSum = 0;              // +0x15cf4 sum of killed enemies' attack
    int  collectedThisTurn = 0;            // +0x15e30 pretzel-mode score collected this turn
    int  collectedScratch = 0;             // +0x15e34 pretzel-mode scratch, zeroed with +0x15e30
    bool killFeedsTrophy = false;          // +0x14d9d set 1 by MatchTile @0x2ac40:238 (special-enemy kill);
                                           //   NO reader anywhere in analysis/decomp -> purpose UNKNOWN
    // ==== end extras: chain.cpp ====
    // ==== extras: combat.cpp (only the worker owning combat.cpp edits this block) ====
    // Helpers called by the combat flow (bodies in src/combat.cpp).
    void ProcessVampires();        // @ 0x2d18c vampires self-heal after incoming damage
    void ProcessCripples();        // @ 0x2f558 werewolf: normal enemies with armour > HP die
    int  GetScore() const;         // @ 0x2f868 run score (classic / sprint / pretzel formulas)
    bool survivedSprint = false;   // +0x15e71 challenge-100-turn flag (EndOfTurn -> CheckEndGame/GetScore)
    int  classRuns[CLASS_COUNT] = {};          // +0x14e04+i*0x184 per-class death count
    int  classKills[CLASS_COUNT] = {};         // +0x14e08+i*0x184 per-class kill total
    int  classBestScore[CLASS_COUNT][3] = {};  // +0x14e0c/10/14+i*0x184 best score (normal/hard/harder)
    // ==== end extras: combat.cpp ====
    // ==== extras: resources.cpp (only the worker owning resources.cpp edits this block) ====
    void Healing();                // @ 0x28c0c Faun racial heal to 25% max HP (body belongs to combat.cpp, not written)
    int  attackBonus = 0;          // +0xc184 skull-attack bonus (items stat1 + STR boosts); skull value = c180+c184
    int  boostStrCount = 0;        // +0xc1ac sum of Item+0xb4 = stat8 over owned items (UpdateStats @0x26c94:91)
    int  boostDexCount = 0;        // +0xc1b4 item stat18 count
    int  boostVitCount = 0;        // +0xc1bc item stat19 count
    int  boostLukCount = 0;        // +0xc1c4 item stat9 count
    int  boostLukChance = 0;       // +0xc1dc = boostLukCount * Skill::GetStatChance() (UpdateStats @0x26c94:153-155)
    int  boostVitChance = 0;       // +0xc1e4 = boostVitCount * Skill::GetStatChance()
    int  boostStrChance = 0;       // +0xc1ec = boostStrCount * Skill::GetStatChance()
    int  boostDexChance = 0;       // +0xc1f4 = boostDexCount * Skill::GetStatChance()
    bool reviveAvailable = false;  // +0x15cee gate of the revive branch AddResource @0x32210:85-89
                                   //   (writer that sets it =1 NOT FOUND in analysis/decomp — see PROVENANCE.md)
    bool reviveUsed = false;       // +0x15cec re-entrancy guard around CheckEndGame (set/cleared @0x32210:86-88;
                                   //   the heal-to-full + achievement 0x3d branch is CheckEndGame @0x2f960:42-55)
    // ==== end extras: resources.cpp ====
    // ==== extras: tile.cpp (only the worker owning tile.cpp edits this block) ====
    int  GetSpecialEnemyCount() const;      // @ 0x24704 skulls with enemyType != 0 (implemented in tile.cpp)
    void SetTimeToPromotion(int t);         // @ 0x2985c (implemented in tile.cpp)
    // Enemy ability helpers called by Tile::ActivateAbility (@ 0x64c78). Bodies belong to other
    // systems (analysis/combat/ENEMY_STATS.md); declared here so tile.cpp compiles.
    void RunAwayEnemy(Tile &t);             // @ 0x300d4  Golden
    void HealOrBless(Tile &t);              // @ 0x34740  Healer
    void MakeAcidPotion(Tile &t);           // @ 0x2ea40  Acidic
    void IgniteTiles(Tile &t);              // @ 0x29be0  Flaming
    void FreezeTiles(Tile &t);              // @ 0x2e898  Freezing
    void ExplodeEnemy(Tile &t);             // @ 0x2b2e4  Kamikaze
    void ConvertSwordToSkull(Tile &t);      // @ 0x2eb4c  Overseer
    void StealGold(Tile &t);                // @ 0x34a68  Thief
    void JumpEnemy(Tile &t);                // @ 0x2f370  Chaotic
    void StabAttack(Tile &t);               // @ 0x2f250  Assassin
    void MimicDefence(Tile &t);             // @ 0x2e84c  Doppelganger
    void ConvertRandomTiles(Tile &t);       // @ 0x2e6bc  Alchemist
    void TeleportEnemy();                   // @ 0x29ad8  Unstable (original takes no Tile*)
    void BreakRandomShieldSword(Tile &t);   // @ 0x2e55c  Trampling
    void LowerSkulls(Tile &t);              // @ 0x2f154  Greasy
    void AttractSkulls(Tile &t);            // @ 0x2e22c  Alpha
    void AttractShieldsSwords(Tile &t);     // @ 0x2defc  Magnetic
    void ThrowSkull(Tile &t);               // @ 0x2eeb0  Brute
    void DestroyUpgrade(Tile &t);           // @ 0x2ecb8  Devourer
    void FleeUp(Tile &t);                   // @ 0x2eda0  Fleeing
    void Resurrect(Tile &t);                // @ 0x2d350  Lich
    // ==== end extras: tile.cpp ====
    // ==== extras: enemy_abilities.cpp (only the worker owning enemy_abilities.cpp edits this block) ====
    // Bodies for the 21 ability hooks declared in the tile.cpp block above (implemented in
    // src/enemy_abilities.cpp). New declarations needed by those bodies:
    int  GetNormalPotionCount() const;      // @ 0x24804 potions that are not acid/mana/explosive
    // ==== end extras: enemy_abilities.cpp ====
    // ==== extras: save.cpp (only the worker owning save.cpp edits this block) ====
    // All-10-classes persistence (Game::SerialiseClasses @ 0x24d98 / DeserialiseClasses @ 0x24d54;
    // the original uses the GameState buffer at Game+0x14314, cursor +0x1431c, and Start @0x260ac:174
    // deserialises iff the cursor > 0). Layout is byte-exact: 2-byte header 0x00 0x0a + 10 x
    // CharacterClass::SerialiseFullState @ 0x36494 (47 B each: 26 B SerialiseGameState + level byte
    // + 5 int counters from classRuns/classKills/classBestScore).
    std::vector<uint8_t> classesSave;                     // +0x14314 blob (cursor +0x1431c = size)
    void SerialiseClasses();                              // @ 0x24d98 (Validate mutates -> non-const)
    bool DeserialiseClasses(const std::vector<uint8_t> &bytes);  // @ 0x24d54 (false = header mismatch)
    // ==== end extras: save.cpp ====
    // ==== extras: items.cpp (only the worker owning items.cpp edits this block) ====
    int upgradeGateLevel = 0;        // +0xc220 = sum of Item+0x9c over the owned items (UpdateStats
                                     //   @0x26c94:43 reset, :78-79); gates ItemUpgrade type 2
                                     //   (IsUpgradeAllowed @0x517b8:14 needs < 0x19) and its amounts
                                     // (ItemUpgrade::GetStatUpgrade type 2, IsUpgradeAllowed 2)
    ItemUpgrade RollItemUpgrade(const Item &item);  // ItemUpgrade::Initialise @ 0x523c8 (items.cpp)
    // Game::GetItem @ 0x24610 reads Game+0xc248..0xc258 — those 5 Item* ARE the equipped items
    // (Game::SetItem @ 0x24644 overwrites them from FoldoutMenu::Process @ 0x41fc4 when a shop
    // offer is confirmed; InitNewGame deletes/recreates them). FoldoutMenu::Initialise @ 0x42794
    // passes GetItem(type) as the offer's GenerateStats base, so baseItems[t].get() must return
    // the CURRENT equipped item of type t (menus.cpp reads it through .get()).
    struct EquippedRef { Game *g = nullptr; int t = 0; Item *get() const; };  // items.cpp
    std::array<EquippedRef, 5> baseItems;         // GetItem @ 0x24610 == items[t]
    void InitBaseItems();                         // items.cpp: wire baseItems -> equipped items
    // ==== end extras: items.cpp ====
    // ==== extras: menus.cpp (only the worker owning menus.cpp edits this block) ====
    // Reward menus (FoldoutMenu, analysis/progression/MENUS.md). Transient UI state: the original
    // never saves the open menu or its offers (SAVE_FORMAT.md saves only the HelpMenu), so none of
    // this is serialised. A pending menu blocks the next move (Game::PlayMove) until ChooseMenu.
    // V12: in the original the reward menu freezes the whole game block of Game::Process @ 0x339ac
    // (ProcessInput @ 0x330c0 + ProcessAttack @ 0x311b8 run only while +0x5ae4/+0x5c00/+0x5c94 are
    // 0; the open menu sets +0x5ae4, tools/emu/v12_probe.py seed 12345 move 28), so the enemy
    // attack (state machine +0xbc5c states 1..8) and EndOfTurn run only AFTER the menu closes and
    // its purchase is applied. The rebuild defers them the same way: PlayMove stops when a menu
    // opens, ChooseMenu runs the deferred attack + EndOfTurn when the menu closes.
    // There is ONE global FoldoutMenu (Interface+0xdf0 = Game+0x5adc): Interface::ShowMenu @ 0x48f30
    // calls FoldoutMenu::Initialise @ 0x42794 on it, which sets the open flag (this[8]=1), resets
    // the selections (+0x30/+0x34 = -1) and regenerates the offers in place. A ShowMenu while a
    // menu is open therefore REPLACES the open menu (its offers are discarded, never applied) —
    // no queue exists in the original.
    int  menuType = -1;                                  // FoldoutMenu+0xc
    bool menuPending = false;                            // menu open, awaiting a choice
    bool menuApplying = false, menuReopened = false;     // ChooseMenu apply opened a new menu
                                                         // (FoldoutMenu::Process @ 0x41fc4:57 ->
                                                         // Skill::Activate @ 0x60210 -> Game::Upgrade
                                                         // @ 0x251d4 ShowMenu(2) replaces the shop)
    bool attackPending = false;                          // V12: attack + EndOfTurn deferred by PlayMove
    bool endOfTurnPending = false;                       // K5: menu opened mid-attack defers EndOfTurn
    void FinishTurnAfterMenus();                         // turn.cpp: deferred attack + EndOfTurn
    // V23: token landing order. The original animates each Token (Game::AddToken @ 0x2a934) and
    // Game::Process @ 0x339ac calls AddResource only when it lands (token +0x72 == 0) — after the
    // chain cascade + refill, and an open menu freezes the game block so later tokens land after
    // the menu closes. AddToken queues; DrainTokens lands them in spawn order, stopping at a
    // pending menu (the Process return at +0x5ae4).
    // K12: a queued token also carries its landing frame (Token::Process @0x65200 physics) so a
    // pending flame/spiky label (landed by Process @0x339ac:394/:459 at tokenLabelFrame) can cut
    // the queue in two — tokens still flying when the label kills never land (:176 hp==0 return).
    struct QueuedToken {
        int type = 0;                 // TileType (+0x78)
        int tokenType = 0;            // +0x7c (0 normal, 1 mana, 2 acid, 3 trap)
        int value = 0;                // +0x98
        int flight = 1;               // frames after release until it lands (1 = first frame)
    };
    std::vector<QueuedToken> tokenQueue;
    int  tokenLabelFrame = 0;         // K12: 0 = no label pending, else the label's landing frame
    void DrainTokens();                                  // chain.cpp
    std::array<std::unique_ptr<Item>, 3>  menuShopItems;     // +0x38..0x40 (3 distinct-slot items)
    int  menuShopSkillSlot = -1;                         // +0x110 (0..2, -1 = no skill offer)
    std::unique_ptr<Skill> menuShopSkill;                // +0x114 (one-shot activation on buy)
    std::array<ItemType, 4> menuUpTypes{};               // +0x58..0x64 (4 distinct-slot item types)
    std::array<ItemUpgrade, 4> menuUpgrades{};           // +0x68..0x74 (rolled per slot)
    std::array<std::unique_ptr<Skill>, 4> menuLevelSkills;   // +0x48..0x54; rows 0..menuSkillRows-1
                                                             // are skill offers, the rest stat boosts
    int  menuSkillRows = 0;                              // +0x78
    void OpenMenu(MenuType m);                           // FoldoutMenu::Initialise @ 0x42794 (menus.cpp)
    void ChooseMenu(int choice, int choice2 = -1);       // apply path of FoldoutMenu::Process @ 0x41fc4
    bool IsSkillUnlocked(int id) const;                  // Game::IsSkillUnlocked @ 0x248e8 (menus.cpp)
    std::set<int> GetClassSkillPool() const;             // CharacterClass::GetSkillPool @ 0x3707c
    // ==== end extras: menus.cpp ====
    // ==== extras: skills.cpp (only the worker owning skills.cpp edits this block) ====
    // Game skill-effect functions (Skill::Activate @ 0x60210 dispatch targets).
    // GetNormalPotionCount @ 0x24804 is declared in the enemy_abilities.cpp block (R9 owns it).
    void FireBreath();                            // @ 0x29cfc Dragonkin racial, called after a
                                                  //   skill activation (ProcessInput @0x49738:93-96)
    int  GetNormalSwordCount() const;        // @ 0x2484c swords that are not magic/arrow
    void GrabTiles(int fromType, int toType);      // @ 0x30e40  Heal/Repair/Skill Elixir/Treasure
    void ConvertTiles(int fromType, int toType);   // @ 0x2a440  Golden Touch/Scavenge/Disarm/Dazzle/Masochism
    void FreezeSkulls();                          // @ 0x29eb0  Freeze
    void ShatterDefence();                        // @ 0x28554  Shatter
    void CreateExplosivePotions();                // @ 0x29df4  Explosive Potion
    void CreateManaPotions();                     // @ 0x29f74  Mana Potion
    void CreateArrows();                          // @ 0x2a108  Volley
    void CreateMagicSword();                      // @ 0x2a1cc  Magic Sword
    void CreateChain();                           // @ 0x2a038  Haste
    void Earthquake();                            // @ 0x2a2ac  Earthquake
    void Upgrade();                               // @ 0x251d4  Enchant (opens the upgrade menu)
    void Teleport();                              // @ 0x2ffb4  Teleport
    void DemoteSpecialEnemy();                    // @ 0x2a34c  Exorcise
    void KillSpecialEnemy();                      // @ 0x30178  Banish
    void PromoteNormalEnemy();                    // @ 0x2d454  Big Game Hunter
    void ExplodeArmour();                         // @ 0x31050  Explosive Armour
    void Shock();                                 // @ 0x30404  Shock
    void Awe();                                   // @ 0x302bc  Awe
    void RemoveSkill(Skill *s);                   // @ 0x24eec  Wish removes itself
    // Bodies live in src/skill_board_effects.cpp (R12); declarations kept here (single copy).
    void CollectRandomRow();                      // @ 0x30c8c  Slash
    void CollectRandomBlock();                    // @ 0x30554  Fireball
    void CollectEdgeTiles();                      // @ 0x308c8  Charge
    void CollectBoard();                          // @ 0x30a2c  Wish
    void ConvertColumnToSwords();                 // @ 0x30b88  Hack
    void KillSkullsNextToCoins();                 // @ 0x306fc  Greed
    // Turn-skill state (Skill::Activate writes; consumed by EndOfTurn / board refill).
    bool doubleDamage = false;        // +0xc234 Boost Damage flag for this turn
    int  carryDoubleDamage = 0;       // +0xc238 byte copy of +0x14d94 taken at activation
    int  carryDoubleArmour = 0;       // +0xc239 (Boost Armour)
    int  carryDoubleHealth = 0;       // +0xc23a (Boost Health)
    int  carryDoubleGold = 0;         // +0xc23b (Boost Gold)
    int  carrySteal = 0;              // +0xc384 (Steal; flag itself is stealTokenMode +0xc383)
    int  carryOneHitSkulls = 0;       // +0x1433f (Trap; flag itself is oneHitSkulls +0x1433d)
    int  carryForceCoin = 0;          // +0x14340 (Treasure Chamber; flag forceCoinMode +0x1433e)
    bool berserkActive = false;       // +0x15cef Berserk: kills heal for the enemy's attack
    int  carryBerserk = 0;            // +0x15cf0
    bool runeActive = false;          // +0x15cd9 Rune: collect tiles inside a closed path
    int  carryRune = 0;               // +0x15cda
    int  carryRevive = 0;             // +0x15ced (Last Stand; active flag reviveUsed +0x15cec)
    bool coinsToSkullsFlag = false;   // +0x15e18 set by Masochism: Skill::Activate @0x60210:165 does
                                      //   ConvertTiles(2,1) -> ConvertTiles @0x2a440:47-49 sets it;
                                      //   cleared EndOfTurn @0x3289c:63, read ProcessInput @0x330c0:294
    bool shockUsed = false;           // +0x15e72 Shock fired this turn (Awe's combo check)
    bool aweUsed = false;             // +0x15e73 Awe fired this turn (Shock's combo check);
                                      // EndOfTurn @ 0x3289c clears both — combat.cpp does not
                                      // model them yet (see tasks/R11/RESULT.md)
    bool turnSkillMarker = false;     // +0x14d94 set when the drawn chain grew by >2 tiles
                                      //   (ProcessInput @0x330c0:247), cleared EndOfTurn @0x3289c:330;
                                      //   gates Awe/Shock/ExplodeArmour/Collect* (`if (… == 0)`)
                                      // (Game::ProcessInput); snapshotted into the carry bytes above
    // ==== end extras: skills.cpp ====
    // ==== extras: skill_board_effects.cpp (only the worker owning that file edits this block) ====
    // Board-collection skills (Slash/Fireball/Charge/Wish/Hack/Greed, src/skill_board_effects.cpp).
    bool slashUsed = false;          // +0x15e19 Slash used this turn (EndOfTurn clears +0x15e19..1c)
    bool fireballUsed = false;       // +0x15e1a Fireball used this turn
    bool chargeUsed = false;         // +0x15e1b Charge used this turn
    bool wishUsed = false;           // +0x15e1c Wish used this turn
    bool slashCombo = false;         // +0x14daa Slash fired, waiting for Hack (Slash+Hack = whole board)
    bool hackCombo = false;          // +0x14da9 Hack fired, waiting for Slash
    void CheckShockAwe();            // @ 0x28318 all four +0x15e19..1c set -> ACH_SHOCK_AWE
    // ==== end extras: skill_board_effects.cpp ====
    // ==== extras: misc.cpp (only the worker owning misc.cpp edits this block) ====
    // Per-run collected-token counters (AddToken @ 0x2a934); normal/bonus pairs feed the
    // Dungeon Sprint score (collectedThisTurn/collectedScratch, chain.cpp block).
    int  killsCollected = 0;         // +0x15e3c skulls collected, normal tokens
    int  coinsCollected = 0;         // +0x15e40 coins collected, normal tokens
    int  shieldsCollected = 0;       // +0x15e44 shields collected, normal tokens (not broken)
    int  killsCollectedBonus = 0;    // +0x15e50 skulls collected, bonus tokens
    int  coinsCollectedBonus = 0;    // +0x15e54 coins collected, bonus tokens
    int  shieldsCollectedBonus = 0;  // +0x15e58 shields collected, bonus tokens (not broken)
    // ==== end extras: misc.cpp ====
    // ==== extras: character_class.cpp (task C1, only that file edits this block) ====
    void SelectClass(int idx);                            // ClassScreen::Process tap @ 0x3a598:506-518 /
                                                           //   FadeOut @ 0x3730c (commit path)
    void SetClassLevel(ClassType c, int l);               // @ 0x24d28 raise-only
    bool IsRaceUnlocked(RaceType r) const;                // @ 0x248a0 (race change at level 5)
    bool IsPerkUnlocked(PerkType p) const;                // @ 0x24dec
    void CheckIfAllClassesMaxed();                        // @ 0x2835c all classes 1..9 at 10 -> ACH_CLASSY
    // ==== end extras: character_class.cpp ====

    // ==== extras: turn.cpp (dungeon_raid_master) ====
    struct Cell { int col, row; };
    bool CanMatchType(const Tile &t, bool fromInput = false) const;   // Tile::CanMatchType @ 0x645ac vs current chain
    int  ComputeChainAttack() const;                  // ProcessTileSelection @ 0x2c8dc (+0xc23c)
    void InitialiseIncoming();                        // InitialiseFlares @ 0x2a548 (+0xc244, +0x14d84)
    int  CountAttackers() const;                      // @ 0x24e2c
    bool IsValidMove(const std::vector<Cell> &path) const;
    bool IsValidPrefix(const std::vector<Cell> &path) const;   // IsValidMove minus the >= 3 rule (turn.cpp)
    bool PlayMove(const std::vector<Cell> &path);     // one full turn (ProcessInput @ 0x330c0 flow)
    int  firePool = 0;                                // +0x1469c flame damage collected by the chain
    uint32_t seedOverride = 0;                        // tests: non-zero replaces time(0) in InitNewGame
    // ==== end extras: turn.cpp ====
    // ==== extras: turn.cpp (task C3 — trophy collection) ====
    // Headless form of the trophy tap: ProcessInput @0x330c0:325-389, the touch release whose
    // matched list holds EXACTLY ONE tile and that tile is a trophy (+0xa0 == 6 type, +0xa4 == 1
    // variant). A single tile that is not a trophy is rejected here (the original clears the list
    // and does nothing else — LAB_000332ba — but its press may consume stream-0 preview draws for
    // a skull, so the harness must only tap trophies). The tap runs NO attack and NO EndOfTurn:
    // those belong to the previous chain move (ProcessInput starts the attack only from
    // 0x5f74 == 0, which MatchTiles sets; the trophy path never calls MatchTiles), so in the
    // rebuild's turn-collapsed model the collection is the whole move.
    bool TapTile(int col, int row);
    // UnlockMenu::Show(ClassType, int) @0x671ec popup record — pure presentation: no game state,
    // no RNG (3-arg Show @0x66fc4 queues sound 0xc; BonusPromptClosed @0x24754 is a no-op; the
    // menu does not gate gameplay beyond Game::Process skipping ProcessInput while visible).
    // The demo reads the last entry: level < 2 -> title "Class unlocked: %s!" @0x5e370 + body
    // CharacterClass::GetUnlockDescription @0x35f24 (g_classUnlockDescriptions @0x6b2e8, index 0
    // empty); level >= 2 -> "%s level up!" @0x5e310 + "The %s class has been upgraded to level
    // %d! ..." @0x5e320. The text itself is not ported (sprintf over .so strings).
    struct UnlockPopup { int classType; int level; };
    std::vector<UnlockPopup> unlockPopups;             // one entry per Show, in order
    void ShowUnlockMenu(int classType, int level);     // records the popup only
    // ==== end extras: turn.cpp C3 ====

    // ==== extras: m8.cpp (task W7 — wall clock) ====
    // Game::SetClockState @ 0x25038: +0x142f9 on/off, +0x142fc = old ^ new (state-changed flag;
    // read by JNI Game_1HasClockStateChanged @ 0x23698). The clock STRING is Java-side:
    // DungeonRaidRenderer.onDrawFrame formats the CURRENT TIME OF DAY with
    // SimpleDateFormat("h:mm a") and calls SetClockString every frame while IsClockEnabled
    // (reads +0x142f9, JNI wrapper @ 0x234dc) — it is a wall clock, not a run timer.
    // Default OFF: DungeonRaidSurfaceView.java:121 getBoolean("clock", false); persisted under
    // prefs key "clock" (java:346; MODES_SCREENS.md:145).
    bool clockEnabled = false;       // +0x142f9
    bool clockStateChanged = false;  // +0x142fc
    // ==== extras: A24 settings toggles (GameMenu::Process @0x44564) ====
    // Help hints on/off: Game::SetHelpState @0x2505c (+0x142f8); gates every tip
    // (HelpMenu::Initialise @0x48348 checks the seen bits; UpdateHelpState @0x24fb0).
    // Java does NOT persist it in prefs — it is rebuilt from the save's help word
    // (DeserialiseState1 @0x2785c:198 SetHelpState(+0x103d8 != 0)). Default ON (all tips).
    bool helpEnabled = true;         // +0x142f8
    // Attack dimming: Game::SetAttackDimmingState @0x25010 (+0x142fa); Game::Render
    // @0x25924:106 draws the dim quad (+0xbcd4) during attack states 1-7 ONLY when set.
    // Java default true (SurfaceView.java:122), persisted prefs "dim" (java:350).
    bool attackDimming = true;       // +0x142fa
    // High framerate / smooth: Game::SetHighFramerate @0x24fe8 (+0x142fb). Read ONLY by
    // GameMenu::Process @0x44564:82-85 for the toggle quad UV — no timing use anywhere
    // (A28b: dt = clock() CPU seconds, UpdateTime @0x258e0; timers are dt-based).
    // Java default true (java:124), persisted prefs "smooth" (java:354).
    bool highFramerate = true;       // +0x142fb
    // ==== end extras: A24 settings ====

    int MaxHp() const { return baseMaxHp + bonusMaxHp; }
    CharacterClass &ActiveClass();
    bool HavePerk(PerkType p) const;
    bool HaveFlaw(FlawType f) const;
    RaceType Race() const;

    Platform &fx() { return *platform_; }

private:
    Platform defaultPlatform_;
    Platform *platform_;
};

// Tile::GetDamageType @ 0x644bc mirror (chain.cpp); chainIt is t's node in g.chain.
int ChainDamageType(Game &g, Tile &t, std::list<Tile *>::iterator chainIt);

} // namespace dr
