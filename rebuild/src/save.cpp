// Save system: Game::SerialiseState / DeserialiseState / DeserialiseState2 (v3 writer + reader)
// plus the Tile/Item/Skill/CharacterClass/HelpMenu block serialisers they call.
// Source: analysis/state/SAVE_FORMAT.md; exact order/widths verified against the decomp files cited
// on each function. UI calls (Interface::, Label::, SetHelpState) are not rebuilt.
#include "dr/game.h"
#include "dr/character_class.h"
#include "dr/item.h"
#include "dr/skill.h"
#include "dr/rng.h"
#include <cstdint>
#include <vector>

namespace dr {
namespace {

// GameState::WriteChar/WriteInteger @ 0x47378 / 0x4738c; ReadChar/ReadInteger @ 0x473bc / 0x473cc.
// Integers are 4 bytes little-endian (4x char).
struct Writer {
    std::vector<uint8_t> bytes;
    void Char(uint8_t v) { bytes.push_back(v); }
    void Int(int v) {
        unsigned u = static_cast<unsigned>(v);
        for (int s = 0; s < 32; s += 8) bytes.push_back(static_cast<uint8_t>(u >> s));
    }
};

struct Reader {
    const std::vector<uint8_t> &bytes;
    size_t pos = 0;
    int Char() { return pos < bytes.size() ? bytes[pos++] : 0; }
    int Int() {
        unsigned u = 0;
        for (int s = 0; s < 32; s += 8) u |= static_cast<unsigned>(static_cast<uint8_t>(Char())) << s;
        return static_cast<int>(u);
    }
};

int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Item id (Item+0x84) is an index into the per-type name table; the writer stores
// id - firstCharOfNameTable(type) (Item::Serialise @ 0x4bb90 switch on Item+0x80).
// The base is the low end of the type's id range (Item::Initialise(ItemType)).
int ItemId(const Item &it) { return it.id; }
int ItemNameBase(ItemType t) { return ItemIdBase(t); }

// Tile::Serialise @ 0x62c28 — state byte only for types 0/3/4/6; visibility byte always;
// flame damage byte when flaming; full enemy block for skulls.
void SerialiseTile(Writer &w, const Tile &t)
{
    switch (t.type) {
    case TILE_POTION: w.Char(t.explosivePotion ? 1 : t.manaPotion ? 2 : t.acidPotion ? 3 : 0); break;
    case TILE_SWORD:  w.Char(t.magicSword ? 1 : t.broken ? 2 : t.arrow ? 3 : 0); break;
    case TILE_SHIELD: w.Char(t.broken ? 1 : 0); break;
    case TILE_SPECIAL: w.Char(static_cast<uint8_t>(t.specialVariant)); break;
    default: break;   // TILE_SKULL / TILE_COIN write no state byte
    }
    w.Char(t.frozen ? 1 : t.flaming ? 2 : 0);
    if (t.flaming) w.Char(static_cast<uint8_t>(t.flameDamage));
    if (t.type == TILE_SKULL) {
        w.Int(t.attack); w.Int(t.defence); w.Int(t.health); w.Int(t.maxHealth);
        w.Char(static_cast<uint8_t>(t.enemyType));
        w.Char(static_cast<uint8_t>(t.countdown));
        w.Char(t.promoted ? 1 : 0);
        w.Char(static_cast<uint8_t>(t.poisonTurns));                       // tile+0x248
        if (t.poisonTurns) {
            w.Char(static_cast<uint8_t>(t.poison));                        // tile+0x24c
            if (t.poison) w.Char(static_cast<uint8_t>(t.poisonAddedThisTurn));  // tile+0x250
        }
    }
}

// Tile::Deserialise2 @ 0x6508c — mirrors Serialise; newFormat (=1) enables the shield-broken byte.
void DeserialiseTile(Reader &r, Tile &t, bool newFormat)
{
    switch (t.type) {
    case TILE_POTION: {
        int s = r.Char();
        if (s == 1) t.explosivePotion = true;        // MakeExplosivePotion
        else if (s == 2) t.manaPotion = true;        // MakeManaPotion
        else if (s == 3) t.acidPotion = true;        // MakeAcidPotion
        break;
    }
    case TILE_SWORD: {
        int s = r.Char();
        if (s == 1) t.magicSword = true;             // MakeMagicSword
        else if (s == 2) t.broken = true;            // MakeBroken
        else if (s == 3) t.arrow = true;             // MakeArrow
        break;
    }
    case TILE_SHIELD:
        if (newFormat && r.Char() == 1) t.broken = true;
        break;
    case TILE_SPECIAL: t.specialVariant = r.Char(); break;   // SetSpecialType
    default: break;
    }
    int vis = r.Char();
    if (vis == 1) t.frozen = true;                   // SetFreezeVisibility(true)
    else if (vis == 2) t.flaming = true;             // CONFIRMED: Deserialise2 @0x6508c:58
    if (t.flaming) t.flameDamage = r.Char();
    if (t.type == TILE_SKULL) {
        t.attack = r.Int(); t.defence = r.Int(); t.health = r.Int(); t.maxHealth = r.Int();
        t.enemyType = static_cast<EnemyType>(r.Char());
        t.countdown = r.Char();
        t.promoted = (r.Char() == 1);
        t.poisonTurns = r.Char();
        if (t.poisonTurns) {
            t.poison = r.Char();
            if (t.poison) t.poisonAddedThisTurn = r.Char();
        }
        // CONFIRMED: Deserialise2 @0x6508c:96-99 sets tile+0x254 = 1 and tile+0x258 = DAT_000651fc
        // when +0x248 (poison turns) != 0 — no rebuild fields for them.
    }
}

// Item::Serialise @ 0x4bb90 — id byte, name indices, label, 20-bit stat mask, then the K non-zero
// stat ints in slot order. (QueueAchievement on >7 upgrades is UI-side, not rebuilt.)
void SerialiseItem(Writer &w, const Item &it)
{
    w.Char(static_cast<uint8_t>(ItemId(it) - ItemNameBase(it.type)));
    w.Char(static_cast<uint8_t>(it.nameIndex[0]));
    w.Char(static_cast<uint8_t>(it.nameIndex[1]));
    w.Char(static_cast<uint8_t>(it.label));
    uint32_t mask = 0;
    for (int i = 0; i < ITEM_STAT_SLOTS; ++i)
        if (it.stats[i] != 0) mask |= 1u << i;
    w.Int(static_cast<int>(mask));
    for (int i = 0; i < ITEM_STAT_SLOTS; ++i)
        if (it.stats[i] != 0) w.Int(it.stats[i]);
}

// Item::Deserialise2 @ 0x4d858 — 0xff name indices mean "none" (-1); ClearStats @ 0x4bac4 zeroes
// stats[0..19] (this+0x94..0xe8) BEFORE the mask is read, so slots absent from the mask end up 0;
// flagged slots get 1, then each non-zero slot is replaced by the next int from the stream.
void DeserialiseItem(Reader &r, Item &it)
{
    it.id = ItemNameBase(it.type) + r.Char();        // id byte is relative to the type's base
    int p = r.Char(); if (p == 0xff) p = -1; it.nameIndex[0] = p;
    int n = r.Char(); if (n == 0xff) n = -1; it.nameIndex[1] = n;
    it.label = r.Char();
    it.stats.fill(0);                                // ClearStats @ 0x4bac4 (before ReadInteger mask)
    uint32_t mask = static_cast<uint32_t>(r.Int());
    for (int i = 0; i < ITEM_STAT_SLOTS; ++i)
        if ((mask >> i) & 1u) it.stats[i] = 1;
    for (int i = 0; i < ITEM_STAT_SLOTS; ++i)
        if (it.stats[i] != 0) it.stats[i] = r.Int();
}

// Skill::Serialise @ 0x6009c — type, maxCharge, charge, rank.
void SerialiseSkill(Writer &w, const Skill &s)
{
    w.Char(static_cast<uint8_t>(s.type));
    w.Char(static_cast<uint8_t>(s.maxCharge));
    w.Char(static_cast<uint8_t>(s.charge));
    w.Char(static_cast<uint8_t>(s.rank));
}

// Skill::Deserialise @ 0x5ffb0 — 0xff maxCharge means "passive" (maxCharge = charge = -1).
void DeserialiseSkill(Reader &r, Skill &s)
{
    s.type = r.Char();
    s.maxCharge = r.Char();
    s.charge = r.Char();
    s.rank = r.Char();
    if (s.maxCharge == 0xff) { s.maxCharge = -1; s.charge = -1; }
}

// CharacterClass::SerialiseGameState @ 0x361ac — 26 bytes: race, (perks[1..3], flaws[1..3]) pairs,
// then 19 bytes read from the array *(CC+0x48) + 4 … +0x4c (indices 1..19). That array is the
// replacement-skills list (master's finding, analysis/validation/DIFFS.md "Fixed by master"), which
// is what the rebuild writes here (was a GUESS claiming zeros).
void SerialiseClassState(Writer &w, const CharacterClass &cc)
{
    w.Char(static_cast<uint8_t>(cc.race));
    for (int i = 1; i < 4; ++i) { w.Char(static_cast<uint8_t>(cc.perks[i])); w.Char(static_cast<uint8_t>(cc.flaws[i])); }
    for (int i = 1; i < 20; ++i) w.Char(static_cast<uint8_t>(cc.replacementSkills[i]));
}

// CharacterClass::DeserialiseGameState @ 0x36168.
void DeserialiseClassState(Reader &r, CharacterClass &cc)
{
    cc.race = static_cast<RaceType>(r.Char());
    for (int i = 1; i < 4; ++i) { cc.perks[i] = r.Char(); cc.flaws[i] = r.Char(); }
    for (int i = 1; i < 20; ++i) cc.replacementSkills[i] = r.Char();
}

// CharacterClass::SerialiseFullState @ 0x36494 — Validate, SerialiseGameState (26 B), level byte,
// then 5 ints at CC+0x58..+0x68, CONFIRMED (was GUESS) = the ClassScreen counters labelled by
// strings 0x5ba74..0x5bac0 (W4b): Games Played, Enemies Killed, Highest Normal Score, Highest
// Hard Score, Highest Harder Score. All five are written by Game::CheckEndGame @ 0x2f960:
// :61-63 ++Games Played and += enemiesKilled (+0x15e14) for the active class, :79-91 best score
// per difficulty 1/2/3 only (Easy has none) — Game+0x14dac + i*0x184 + 0x58/0x5c/0x60/0x64/0x68
// == +0x14e04/08/0c/10/14. The rebuild keeps those 5 ints on Game as classRuns[i] /
// classKills[i] / classBestScore[i][0..2] (combat.cpp:250-257), so these helpers take the class
// index; they are exposed as dr_class fields 54-58 (M8, src/m8.cpp).
void SerialiseFullState(Writer &w, Game &g, int i)
{
    CharacterClass &c = *g.classes[i];
    c.Validate();
    SerialiseClassState(w, c);
    w.Char(static_cast<uint8_t>(c.level));
    w.Int(g.classRuns[i]);
    w.Int(g.classKills[i]);
    w.Int(g.classBestScore[i][0]);
    w.Int(g.classBestScore[i][1]);
    w.Int(g.classBestScore[i][2]);
}

// CharacterClass::DeserialiseFullState @ 0x3643c — DeserialiseGameState, level = ClampI(char,0,10)
// (forced back to 1 when klass == 0), the 5 ints, Validate.
void DeserialiseFullState(Reader &r, Game &g, int i)
{
    CharacterClass &c = *g.classes[i];
    DeserialiseClassState(r, c);
    c.level = ClampI(r.Char(), 0, 10);
    if (c.klass == CLASS_ADVENTURER) c.level = 1;
    g.classRuns[i] = r.Int();
    g.classKills[i] = r.Int();
    g.classBestScore[i][0] = r.Int();
    g.classBestScore[i][1] = r.Int();
    g.classBestScore[i][2] = r.Int();
    c.Validate();
}

// Bonus-roll chance fields in Game order: +0xc1d8 coin, +0xc1e0 potion, +0xc1e8 skull, +0xc1f0 shield (MatchTile switch)
// (bonusChance is indexed by TileType — review fix).
static const TileType kBonusChanceOrder[4] = {TILE_COIN, TILE_POTION, TILE_SKULL, TILE_SHIELD};

// Game::DeserialiseState2 @ 0x2733c — reader for the 0xff-marker format; version 3 is what this
// build writes, versions 0..2 keep their skip-branches. newFormat = (version != 0).
void DeserialiseState2(Game &g, Reader &r, int newFormat, int version)
{
    g.specialEnemiesSpawned = 0;                     // +0x14da4
    if (version < 3) {
        g.challenge = false;                         // +0x14335 = 0
        if (version < 2) g.activeClass = 0;
    } else {
        int v = r.Char();                            // +0x14330 challenge mode, 0xff = classic
        g.challenge = (v != 0xff);
        if (v != 0xff) g.mode = static_cast<GameMode>(ClampI(v, 0, 1));
        g.unknown15e2c = r.Int();                    // +0x15e2c
        g.chainsMade = r.Int();                      // +0x15e60
        g.chainAvgX10 = r.Char();                    // +0x15e64
        g.longestChain = r.Char();                   // +0x15e68
    }
    if (version >= 2) {
        g.activeClass = r.Char();                    // +0x15cd4
        DeserialiseClassState(r, *g.classes[g.activeClass]);
        g.specialEnemiesSpawned = r.Char();          // +0x14da4
    }
    g.promoteCountdown = 1;                          // +0x14338, reset before the grid
    int last = 0;                                    // packed grid: high nibble = even col, low = odd
    for (int row = 0; row < BOARD_SIZE; ++row)
        for (int col = 0; col < BOARD_SIZE; ++col) {
            int type = (col & 1) ? (last & 0xf) : ((last = r.Char()) >> 4);
            g.board[col][row].SetType(static_cast<TileType>(type), 0, g.difficulty);
        }
    for (int row = 0; row < BOARD_SIZE; ++row)
        for (int col = 0; col < BOARD_SIZE; ++col)
            DeserialiseTile(r, g.board[col][row], newFormat != 0);
    g.turn = r.Int();                                // +0xc
    if (version > 1) g.enemiesKilled = r.Int();      // +0x15e14
    for (TileType tt : kBonusChanceOrder) g.bonusChance[tt][0] = r.Char();   // +0xc1d8/0xc1e0/0xc1e8/0xc1f0
    g.poisonLevel = r.Char();                        // +0x14d88
    r.Char();                                        // reserved byte
    g.promoteCountdown = r.Char();                   // +0x14338
    for (int &u : g.unknownC1A8) u = r.Int();        // +0xc1a8/0xc1b0/0xc1b8/0xc1c0
    g.baseDamage = r.Int();                          // +0xc180
    g.armourBase = r.Int();                          // +0xc1d0
    g.healBase = r.Int();                            // +0xc1c8
    g.baseMaxHp = r.Int();                           // +0xc15c
    g.hp = r.Int();                                  // +0xc158
    g.xpHundreds = r.Int();                          // +0xc210 (xp base + bonus)
    g.upHundreds = r.Int();                          // +0xc218
    g.goldHundreds = r.Int();                        // +0xc214
    g.armour = r.Int();                              // +0xc164
    g.xp = g.xpHundreds % 100;                       // +0xc170
    g.upgradePoints = g.upHundreds % 100;            // +0xc174
    g.gold = g.goldHundreds % 100;                   // +0xc178
    g.xpHundreds -= g.xpHundreds % 100;
    g.upHundreds -= g.upHundreds % 100;
    g.goldHundreds -= g.goldHundreds % 100;
    g.level = g.xpHundreds / 100 + 1;                // +0xc20c
    for (auto &it : g.items) DeserialiseItem(r, *it);
    int savedArmour = g.armour, savedHp = g.hp;      // UpdateStats recomputes bonuses, then restore
    g.armour = 0; g.maxArmour = 0; g.bonusMaxHp = 0;
    g.UpdateStats();
    g.armour = savedArmour; g.hp = savedHp;
    // Interface::UpdateSkullValue/HealthBar/ShieldValue/XPBar/UPBar/GoldBar/SetPoison — UI, not rebuilt.
    int count = r.Char();                            // +0xc368 skill count
    for (int i = 0; i < count; ++i) {
        // the original deserialises into pre-allocated +0xc358 slots (Game::Start creates them);
        // the rebuild allocates on demand (menus.cpp SetSkill appends, so a loaded game can have
        // more skills than a fresh one — V5)
        if (i >= static_cast<int>(g.skills.size())) {
            auto s = std::make_unique<Skill>();
            s->game = &g;
            g.skills.push_back(std::move(s));
        }
        DeserialiseSkill(r, *g.skills[i]);
    }
    g.playerName.clear();                            // player name at Game+0xc1f8, NUL-terminated, max 0x11 bytes
    for (int i = 0; i < 0x11; ++i) { int c = r.Char(); if (c == 0) break; g.playerName += static_cast<char>(c); }
    SetRandSeed(static_cast<uint32_t>(r.Int()), 1);  // RNG streams 1..5 (stream 0 not saved)
    SetRandSeed(static_cast<uint32_t>(r.Int()), 2);
    SetRandSeed(static_cast<uint32_t>(r.Int()), 3);
    SetRandSeed(static_cast<uint32_t>(r.Int()), 4);
    SetRandSeed(static_cast<uint32_t>(r.Int()), 5);
    g.helpState = r.Int();                           // HelpMenu+0x38 (SetHelpState)
    g.difficulty = static_cast<Difficulty>(ClampI(r.Char(), 0, 3));   // +0x14328 (+0x1432c flag skipped)
}

} // namespace

// Game::SerialiseState @ 0x265f4 — writes the v3 save stream (SAVE_FORMAT.md table).
std::vector<uint8_t> Game::SerialiseState() const
{
    Writer w;
    w.Char(0xff);                                        // marker: "state present"
    w.Char(3);                                           // format version this build writes
    w.Char(challenge ? static_cast<uint8_t>(mode) : 0xff);   // +0x14330 / +0x14335
    w.Int(unknown15e2c);                                 // +0x15e2c
    w.Int(chainsMade);                                   // +0x15e60
    w.Char(static_cast<uint8_t>(chainAvgX10));           // +0x15e64
    w.Char(static_cast<uint8_t>(longestChain));          // +0x15e68
    w.Char(static_cast<uint8_t>(activeClass));           // +0x15cd4
    SerialiseClassState(w, *classes[activeClass]);
    w.Char(static_cast<uint8_t>(specialEnemiesSpawned)); // +0x14da4
    for (int row = 0; row < BOARD_SIZE; ++row)           // 6x3 packed tile-type bytes
        for (int col = 0; col < BOARD_SIZE; col += 2) {
            int lo = static_cast<int>(board[col][row].type);
            int hi = static_cast<int>(board[col + 1][row].type);
            w.Char(static_cast<uint8_t>(((lo & 0xf) << 4) | (hi & 0xff)));
        }
    for (int row = 0; row < BOARD_SIZE; ++row)           // 36 x Tile::Serialise, row-major
        for (int col = 0; col < BOARD_SIZE; ++col)
            SerialiseTile(w, board[col][row]);
    w.Int(turn);                                         // +0xc
    w.Int(enemiesKilled);                                // +0x15e14
    for (TileType tt : kBonusChanceOrder) w.Char(static_cast<uint8_t>(bonusChance[tt][0]));  // +0xc1d8..0xc1f0
    w.Char(static_cast<uint8_t>(poisonLevel));           // +0x14d88
    w.Char(0);                                           // reserved
    w.Char(static_cast<uint8_t>(promoteCountdown));      // +0x14338
    for (int u : unknownC1A8) w.Int(u);                  // +0xc1a8/0xc1b0/0xc1b8/0xc1c0
    w.Int(baseDamage);                                   // +0xc180
    w.Int(armourBase);                                   // +0xc1d0
    w.Int(healBase);                                     // +0xc1c8
    w.Int(baseMaxHp);                                    // +0xc15c
    w.Int(hp);                                           // +0xc158
    w.Int(xp + xpHundreds);                              // +0xc170 + 0xc210
    w.Int(upgradePoints + upHundreds);                   // +0xc174 + 0xc218
    w.Int(gold + goldHundreds);                          // +0xc178 + 0xc214
    w.Int(armour);                                       // +0xc164
    for (const auto &it : items) SerialiseItem(w, *it);  // +0xc25c.. item vector
    w.Char(static_cast<uint8_t>(skills.size()));         // +0xc368
    for (const auto &s : skills) SerialiseSkill(w, *s);
    for (size_t i = 0; i < playerName.size() && i < 0x10; ++i) w.Char(static_cast<uint8_t>(playerName[i]));
    w.Char(0);                                           // player name +0xc1f8, NUL-terminated
    w.Int(static_cast<int>(GetRandSeed(1)));             // RNG streams 1..5
    w.Int(static_cast<int>(GetRandSeed(2)));
    w.Int(static_cast<int>(GetRandSeed(3)));
    w.Int(static_cast<int>(GetRandSeed(4)));
    w.Int(static_cast<int>(GetRandSeed(5)));
    w.Int(helpState);                                    // HelpMenu+0x38
    w.Char(static_cast<uint8_t>(difficulty));            // +0x14328
    return w.bytes;
}

// Game::DeserialiseState @ 0x27c8c — byte 0 == 0xff selects the new format (version in byte 1);
// anything else is the legacy layout (DeserialiseState1 @ 0x2785c, not rebuilt).
bool Game::DeserialiseState(const std::vector<uint8_t> &bytes)
{
    Reader r{bytes};
    if (r.Char() != 0xff) return false;                  // legacy save — not rebuilt
    int version = r.Char();
    DeserialiseState2(*this, r, version != 0 ? 1 : 0, version);
    return true;
}

// Game::SerialiseClasses @ 0x24d98 — the all-10-classes blob, byte-exact: header bytes 0x00 0x0a
// then CharacterClass::SerialiseFullState x10 (47 B each, 472 B total). The original writes into
// the GameState buffer at Game+0x14314 (cursor +0x1431c, which Game::Start @ 0x260ac:174-176 uses
// as "blob present"; the rebuild keeps the finished blob in classesSave instead).
void Game::SerialiseClasses()
{
    Writer w;
    w.Char(0x00);                                        // header byte 0 (ignored on read)
    w.Char(0x0a);                                        // header byte 1: class count, tested on read
    for (int i = 0; i < CLASS_COUNT; ++i) SerialiseFullState(w, *this, i);
    classesSave = std::move(w.bytes);
}

// Game::DeserialiseClasses @ 0x24d54 — read header; unless byte 1 == 10 the original leaves every
// class untouched (returns false here), else DeserialiseFullState x10.
bool Game::DeserialiseClasses(const std::vector<uint8_t> &bytes)
{
    Reader r{bytes};
    r.Char();                                            // header byte 0
    if (r.Char() != 10) return false;
    for (int i = 0; i < CLASS_COUNT; ++i) DeserialiseFullState(r, *this, i);
    return true;
}

} // namespace dr
