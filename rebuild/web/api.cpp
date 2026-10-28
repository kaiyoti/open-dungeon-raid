// Web front-end API (W1): extern "C" entry points exported to JS via EMSCRIPTEN_KEEPALIVE.
// Presentation only — no game logic lives here; everything delegates to dr::Game.
// Packed formats (see web/template.html for the JS side):
//   dr_tile(col,row) bitfield: 0-2 type, 3-8 enemyType, 9-10 specialVariant, 11 frozen,
//     12 flaming, 13 manaPotion, 14 acidPotion, 15 explosivePotion, 16 magicSword,
//     17 arrow, 18 broken, 19 promoted, 20 inChain, 21-23 faceFrame (skull face, +0x23c).
//   dr_play(ptr,len): len bytes, each col + row*6 (0..35), in draw order.
//   dr_menu_offer(i, field): field 0 = kind (0 none, 1 item, 2 skill, 3 upgrade, 4 stat),
//     1 = id, 2 = rank/itemId/upgradeValue, 3 = maxCharge/itemLabel/upgradeStat, 4 = charge;
//     stat payload fields 5..12 (hp/heal/armour/damage/luck/vit/str/dex chances);
//     item stats fields 10..29 = stats[0..19].
#include "dr/game.h"
#include "dr/character_class.h"
#include "dr/item.h"
#include "dr/m8.h"
#include "dr/skill.h"
#include "dr/rng.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

using namespace dr;

namespace {

// M9 event kinds (task A3) — one ordered log per action; layout documented in
// milestones/M8/API_CONTRACT.md "M9 events". Presentation plumbing only: recording these
// changes no game state and draws no RNG (the base AddLabel/AddParticles/AddFlare calls below
// keep consuming the same stream-0 draws the original's presentation consumes).
enum EventKind {
    EV_CHAIN_TILE = 0,   // p0 col, p1 row, p2 tile type — a tile of the drawn chain, in draw order
    EV_TILE_MATCHED = 1, // p0 col, p1 row, p2 tile type — MatchTile @0x2ac40 set +0x88 = 1
    EV_TILE_FELL = 2,    // p0 col, p1 fromRow, p2 toRow, p3 spawned — MoveMatchedTiles @0x2fd7c
    EV_TOKEN = 3,        // p0 tokenTileType, p1 fromCol, p2 fromRow, p3 tokenType, p4 value, p5 flight
    EV_LABEL = 4,        // p0 kind, p1 col (-1 = state-anchored), p2 row, p3 value
    EV_PARTICLES = 5,    // p0 kind, p1 col, p2 row
    EV_FLARE = 6,        // p0 colour kind, p1 col, p2 row
    EV_ATTACKER = 7,     // p0 col, p1 row, p2 attack, p3 enemyType — the attack set (0x2a548)
    EV_INCOMING = 8,     // p0 incoming, p1 blocked, p2 broken, p3 hpBefore, p4 hpAfter (0x311b8 5/6)
    EV_SHIELD_BREAK = 9, // p0 armourLost — ProcessAttack @0x311b8 state 5 break
    EV_HEALTH = 10,      // p0 hp — Interface::SetHealth
    EV_ARMOUR = 11,      // p0 armour — Interface::UpdateShieldValue
    EV_MENU = 12,        // p0 menuType — Interface::ShowMenu @0x48f30
    EV_SOUND = 13,       // p0 sound id (Sound enum)
    EV_PHASE_ATTACK = 14 // marker: the enemy-attack phase starts here (no payload)
};
constexpr int EVENT_FIELDS = 13;  // [0]=kind [1]=phase [2..7]=payload [8..12]=token flight draws
                                  // (A4b: TOKEN event only — the stream-0 draws u[0..4] of
                                  // Token::InitialiseFromTile @0x653d0, fixed-point x10000;
                                  // 0 for every other event kind)

// Platform front end: records queued sounds + floating labels for JS (art build plays sounds,
// both builds draw labels) AND the M9 ordered event log (task A3).
struct WebPlatform : Platform {
    std::vector<int> sounds;
    struct Label { int kind; int col; int row; int value; int tileType; };   // col/row -1 = state-anchored
    std::vector<Label> labels;                                 // (Game vector +0xb8f4, HUD.md §3)
    std::vector<std::array<int, EVENT_FIELDS>> events;         // M9: one ordered log per action
    // F5: one entry per HelpMenu::Initialise request: g_helpText index N + the anchor cell
    // (the original passes tile+0x54/+0x58; col/row -1 = the Interface::ShowHelp* fixed-anchor
    // wrappers, resolved on the page from the decomp constants).
    struct Help { int n; int col; int row; };
    std::vector<Help> helps;
    int phase = 0;   // 0 = chain/match phase, 1 = enemy attack (set by the attack hooks)
    // HelpMenu::Initialise @0x48348 (seen-bit test + clear is the page's; the engine only
    // reports the trigger). Base is empty (platform.h:23) — no RNG draw to consume.
    void ShowHelp(int n, const Tile *t) override {
        helps.push_back({n, t ? t->col : -1, t ? t->row : -1});
    }
    void Ev(int kind, int p0 = -1, int p1 = -1, int p2 = -1,
            int p3 = -1, int p4 = -1, int p5 = -1,
            int p6 = 0, int p7 = 0, int p8 = 0, int p9 = 0, int p10 = 0)
    {
        events.push_back({{ kind, phase, p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10 }});
    }
    void ClearEvents() { events.clear(); phase = 0; }
    void QueueSound(Sound s) override
    {
        sounds.push_back(static_cast<int>(s));
        Ev(EV_SOUND, static_cast<int>(s));
    }
    // W4c: capture the label the original adds (Game::AddSimpleLabel @0x28744/@0x2d0d4). MUST call
    // the base implementation (platform.h:22) — it consumes the stream-0 draw the original's
    // SimpleLabel::Initialise @0x5fbc4:27 consumes; skipping it drifts the trophy countdown.
    void AddLabel(int kind, const Tile *t, int value) override {
        Platform::AddLabel(kind, t, value);
        labels.push_back(Label{kind, t ? t->col : -1, t ? t->row : -1, value,
                               t ? static_cast<int>(t->type) : -1});
        Ev(EV_LABEL, kind, t ? t->col : -1, t ? t->row : -1, value,
           t ? static_cast<int>(t->type) : -1,    // p4 = source tile type (A4b icon)
           static_cast<int>(labelRise * 10000.0f + (labelRise >= 0 ? 0.5f : -0.5f)));   // p5 = rise speed x10000 (A8)
    }
    void AddParticles(const Tile *t, int kind) override {
        Platform::AddParticles(t, kind);   // stream-0 draw (platform.h:25) — keep it
        Ev(EV_PARTICLES, kind, t ? t->col : -1, t ? t->row : -1);
    }
    void AddFlare(const Tile *t, int colourKind) override {
        Platform::AddFlare(t, colourKind); // stream-0 draw (platform.h:26) — keep it
        Ev(EV_FLARE, colourKind, t ? t->col : -1, t ? t->row : -1);
    }
    void ShowMenu(MenuType m) override { Ev(EV_MENU, static_cast<int>(m)); }
    void OnHealthChanged(int hp) override { Ev(EV_HEALTH, hp); }
    void OnArmourChanged(int armour) override { Ev(EV_ARMOUR, armour); }
    void TileMatched(const Tile &t) override { Ev(EV_TILE_MATCHED, t.col, t.row, static_cast<int>(t.type)); }
    void TileFell(int col, int fromRow, int toRow, bool spawned) override
    { Ev(EV_TILE_FELL, col, fromRow, toRow, spawned ? 1 : 0); }
    void TokenQueued(const Tile &from, int tokenTileType, int tokenType,
                     int value, bool bonus, int flight, const float *u5) override
    {
        // A4b: fields 8..12 = the token's stream-0 flight draws (u[0..4], x10000 fixed point)
        // so the renderer replays Token::Process @0x65200 with the ORIGINAL values (M9 rule:
        // reproduce the look from the same draws, not new ones). 0 = no draws (dead token).
        int f[5] = {0, 0, 0, 0, 0};
        for (int i = 0; i < 5; ++i)
            if (u5) f[i] = static_cast<int>(u5[i] * 10000.0f + 0.5f);
        Ev(EV_TOKEN, tokenTileType, from.col, from.row, tokenType, value,
           (flight << 1) | (bonus ? 1 : 0),   // p5 = flight*2 + bonus flag (K12 frames)
           f[0], f[1], f[2], f[3], f[4]);
    }
    void Attacker(const Tile &t) override
    {
        if (phase == 0) { phase = 1; Ev(EV_PHASE_ATTACK); }
        Ev(EV_ATTACKER, t.col, t.row, t.GetAttack(), static_cast<int>(t.enemyType));
    }
    void IncomingResolved(int incoming, int blocked, int broken, int hpBefore, int hpAfter) override
    {
        if (phase == 0) { phase = 1; Ev(EV_PHASE_ATTACK); }
        Ev(EV_INCOMING, incoming, blocked, broken, hpBefore, hpAfter);
    }
    void ShieldBreak(int armourLost) override
    {
        if (phase == 0) { phase = 1; Ev(EV_PHASE_ATTACK); }
        Ev(EV_SHIELD_BREAK, armourLost);
    }
};

WebPlatform g_web;
std::unique_ptr<Game> g_game;
std::vector<uint8_t> g_save;     // dr_save() result
std::vector<uint8_t> g_stage;    // dr_buf() staging buffer for dr_load()
std::vector<uint8_t> g_classes;  // all-10-classes blob (Game::SerialiseClasses @ 0x24d98): class
                                 // meta state persists across dr_new_game calls (M8 "persists")
std::vector<uint8_t> g_meta;     // dr_meta_save() result (container, see dr_meta_save below)
ScoreBook g_scores;              // ScoreScreen @ 0x5c398 state, rebuild/src/m8.cpp
std::vector<Game::Cell> g_preview;  // chain staged by dr_preview, read by dr_preview_tile
bool g_scored = false;           // AddScore runs once per run (CheckEndGame gate @ 0x2f960:27)
std::string g_name;              // W6: player name carried across dr_new_game calls — Game+0xc1f8
                                 // persists across games in the original (NewGame @ 0x2de68)

Game *G() { return g_game.get(); }

void Put32(std::vector<uint8_t> &b, uint32_t v)
{
    b.push_back(static_cast<uint8_t>(v & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}
int Get32(const uint8_t *p)
{
    return static_cast<int>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                            (static_cast<uint32_t>(p[2]) << 16) |
                            (static_cast<uint32_t>(p[3]) << 24));
}

// packed byte = col + row*6 (0..35), in draw order (same format as dr_play).
std::vector<Game::Cell> Unpack(const uint8_t *packed, int len)
{
    std::vector<Game::Cell> path;
    if (!packed || len <= 0) return path;
    path.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) {
        const int v = packed[i];
        path.push_back(Game::Cell{v % BOARD_SIZE, v / BOARD_SIZE});
    }
    return path;
}

// W9: keep the session class meta before an object is replaced (dr_new / dr_load). classesSave
// (+0x14314, the blob Game::Start @0x260ac:174-176 finds "present") is refreshed at every class
// level-up (src/turn.cpp:425, at CheckEndGame :389) and at every dr_meta_save / dr_meta_load, so
// it is never older than g_classes — carrying it forward means an in-run level-up survives
// Continue the way it does in the original (one Game for the whole session).
void CarryClasses()
{
    if (g_game && !g_game->classesSave.empty()) g_classes = g_game->classesSave;
}

} // namespace

extern "C" {

// ---- lifecycle ---------------------------------------------------------------------
// W9: the ONE Game of the session exists before any screen reads meta, exactly like the
// original: Game ctor + Game::Start @0x260ac runs CharacterClass::Initialise for all 10 classes
// (:168-172 — the rebuild Game ctor does that half, src/game.cpp:13) and then loads the classes
// blob that is already there when Start runs (:174-176 `if (Game+0x1431c > 0) DeserialiseClasses`)
// — which the page does as its next boot step (dr_meta_load). Until W9 the exports returned
// "no game" defaults on a fresh module (dr_class -> -1, read by the screens as "unlocked").
// Idempotent: main() calls it when the module starts, template.html boot() calls it before
// dr_meta_load, and dr_new_game falls back on it.
EMSCRIPTEN_KEEPALIVE int dr_init()
{
    if (!g_game) {
        g_game = std::make_unique<Game>(&g_web);
        g_game->playerName = g_name;   // Game+0xc1f8 carries (W6)
        if (!g_classes.empty())
            g_game->DeserialiseClasses(g_classes);   // Game::Start @0x260ac:174-176
    }
    return 1;
}

EMSCRIPTEN_KEEPALIVE void dr_new(uint32_t seed, int difficulty)
{
    CarryClasses();   // W9: the outgoing object's class meta goes to g_classes (see above)
    g_web.sounds.clear();
    g_web.helps.clear();
    g_web.labels.clear();
    g_web.ClearEvents();   // M9 (task A3): a new run starts the event log clean
    g_game = std::make_unique<Game>(&g_web);
    g_game->playerName = g_name;   // W6: Game+0xc1f8 persists across games in the original
    if (!g_classes.empty())
        g_game->DeserialiseClasses(g_classes);   // Game::Start @0x260ac:174-176 — a fresh object
                                                 // must not drop the session's class meta (W9)
    g_game->seedOverride = seed;
    g_game->NewGame(static_cast<Difficulty>(difficulty < 0 || difficulty >= DIFF_COUNT
                                                ? DIFF_NORMAL
                                                : difficulty));
    g_scored = false;   // W3: one ScoreBook insert per run (dr_score_add)
    g_preview.clear();
}

// ---- board -------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int dr_tile(int col, int row)
{
    Game *g = G();
    if (!g || col < 0 || col >= BOARD_SIZE || row < 0 || row >= BOARD_SIZE)
        return TILE_NONE;
    const Tile &t = g->At(col, row);
    int p = static_cast<int>(t.type) & 7;
    p |= (static_cast<int>(t.enemyType) & 63) << 3;
    p |= (t.specialVariant & 3) << 9;
    p |= (t.frozen ? 1 : 0) << 11;
    p |= (t.flaming ? 1 : 0) << 12;
    p |= (t.manaPotion ? 1 : 0) << 13;
    p |= (t.acidPotion ? 1 : 0) << 14;
    p |= (t.explosivePotion ? 1 : 0) << 15;
    p |= (t.magicSword ? 1 : 0) << 16;
    p |= (t.arrow ? 1 : 0) << 17;
    p |= (t.broken ? 1 : 0) << 18;
    p |= (t.promoted ? 1 : 0) << 19;
    p |= (t.inChain ? 1 : 0) << 20;
    p |= (t.faceFrame & 7) << 21;   // +0x23c skull face column (Tile::SetType @0x64ef8:23,29)
    return p;
}

// field: 0 attack, 1 defence, 2 health, 3 maxHealth, 4 countdown
EMSCRIPTEN_KEEPALIVE int dr_enemy(int col, int row, int field)
{
    Game *g = G();
    if (!g || col < 0 || col >= BOARD_SIZE || row < 0 || row >= BOARD_SIZE) return 0;
    const Tile &t = g->At(col, row);
    switch (field) {
    case 0: return t.attack;
    case 1: return t.defence;
    case 2: return t.health;
    case 3: return t.maxHealth;
    case 4: return t.countdown;
    default: return 0;
    }
}

// ---- stats -------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int dr_stat(int id)
{
    Game *g = G();
    if (!g) return 0;
    switch (id) {
    case 0:  return g->hp;
    case 1:  return g->MaxHp();
    case 2:  return g->armour;
    case 3:  return g->maxArmour;
    case 4:  return g->gold;             // bar 0..99 (+ goldHundreds for total)
    case 5:  return g->xp;
    case 6:  return g->upgradePoints;
    case 7:  return g->level;
    case 8:  return g->turn;
    case 9:  return g->enemiesKilled;
    case 10: return g->GetScore();
    case 11: return static_cast<int>((g->xp + g->xpFrac) * 10.0f);       // xp bar permille
    case 12: return static_cast<int>((g->gold + g->goldFrac) * 10.0f);   // gold bar permille
    case 13: return static_cast<int>((g->upgradePoints + g->upFrac) * 10.0f);
    case 14: return g->incomingDamage;
    case 15: return g->baseDamage;
    case 16: return g->poisonLevel;
    case 17: return g->chainsMade;
    case 18: return g->longestChain;
    case 19: return g->activeClass;
    case 20: return static_cast<int>(g->Race());
    case 21: return static_cast<int>(g->skills.size());
    case 22: return g->xpHundreds + g->xp;
    case 23: return g->goldHundreds + g->gold;
    case 24: return g->upgradePoints + g->upHundreds;
    case 27: return g->swordDamage;                     // HUD weapon damage: +0xc17c (Game::UpdateStats @ 0x26c94:191-193)
    case 28: return g->baseDamage + g->attackBonus;     // HUD base damage: +0xc180 + +0xc184 (UpdateStats @ 0x26c94:195-197)
    case 25: return static_cast<int>(g->ActiveClass().klass);
    case 26: return static_cast<int>(g->difficulty);
    // ---- M8 additions (W3) ----
    case 29: return g->poisonLevel;
    // 29 poison shown by Interface::SetPoison @ 0x48d80 (arg = max(+0x14d88,+0x14d84) written
    // back to +0x14d88 by ProcessAttack @ 0x311b8:395-399) = Game+0x14d88 = stat 16's source.
    case 30: return g->incomingDamage;
    // 30 incoming damage shown for the next attack: InitialiseFlares @ 0x2a548 (+0xc244),
    // same source as stat 14.
    // 31 REMOVED (task W7): the HUD clock is the device's WALL CLOCK, not a run counter —
    // DungeonRaidRenderer.onDrawFrame formats the current time of day with
    // SimpleDateFormat("h:mm a") and calls SetClockString every frame while IsClockEnabled.
    // The engine keeps only the on/off flag: dr_clock_enabled() / dr_set_clock() below
    // (Game::SetClockState @ 0x25038, Game+0x142f9 / +0x142fc).
    case 32: return g->chainsMade;
    // 32 "Total Pretzels": GameOverScreen::Process @ 0x469dc case 4 (pretzel branch) reads
    // Game+0x15e60 = chainsMade (same source as stat 17).
    case 33: return g->specialEnemiesSpawned;
    // 33 trophies this run = Game+0x14da4, the trophy-tile spawn counter (MatchTile @ 0x2ac40:240
    // gate `< 3`, :256 ++, reset InitNewGame @ 0x2d7a4:49).
    // UNKNOWN(collected-trophy counter: no such field anywhere in analysis/decomp).
    case 34: return g->chainAvgX10;
    // 34 average chain length x10 = Game+0x15e64 (MatchTiles @ 0x31bcc:128-134 running
    // `crossings*10` average; GameOverScreen::Process @ 0x469dc:345 shows it raw on "Average
    // Length", and the Pretzel Hero score column shows the same raw value). Divide by 10 to show
    // the average itself.
    // ---- U8b: stat-skill row text (Skill::Initialise @0x606a4 sprintf args) ----
    case 40: return g->healBase;                       // +0xc1c8 "Health per potion: %d >> %d"
    case 41: return g->armourBase;                     // +0xc1d0 "Armour per shield: %d >> %d"
    case 42: return g->bonusChance[TILE_COIN][0];      // +0xc1d8  "Chance of bonus coin: "
    case 43: return g->bonusChance[TILE_POTION][0];    // +0xc1e0  "Chance of bonus potion: "
    case 44: return g->bonusChance[TILE_SKULL][0];     // +0xc1e8  "Chance of bonus XP: "
    case 45: return g->bonusChance[TILE_SHIELD][0];    // +0xc1f0  "Chance of bonus shield: "
    case 46: return g->baseMaxHp;                      // U8b: +0xc15c "Max health: %d >> %d"
    default: return 0;
    }
}

// C1 step 3 TEST HARNESS (not game logic): page-side RNG probe (tasks/C1/dbg_c1_page.py).
// dr_rng_trace(1) turns on the engine's R/I draw trace (rng.cpp DrSetRngTrace, prints to
// console.error); dr_seed0() = stream-0 seed (GetRandSeed(0), pairs with native 0x675c8).
EMSCRIPTEN_KEEPALIVE void dr_rng_trace(int on) { dr::DrSetRngTrace(on); }
EMSCRIPTEN_KEEPALIVE int  dr_seed0() { return static_cast<int>(dr::GetRandSeed(0)); }

// U8b TEST HARNESS (not game logic): open a reward menu on demand so the page and the emu
// (tasks/U8b/capture2.py: boot(seed) + FoldoutMenu::Initialise @0x42794) reach the SAME
// RNG state and therefore the SAME offers. OpenMenu (menus.cpp) IS the ported Initialise.
EMSCRIPTEN_KEEPALIVE void dr_open_menu(int kind)
{
    Game *g = G();
    if (g && !g->menuPending) g->OpenMenu(static_cast<MenuType>(kind));
}
// U8b TEST HARNESS (not game logic): per-stream RNG seed (GetRandSeed @ 0x675c8), pairs with
// the emu's NativeGame.rand_seed(stream).
EMSCRIPTEN_KEEPALIVE int dr_rng_seed(int stream) { return static_cast<int>(dr::GetRandSeed(static_cast<unsigned>(stream))); }

// Equipped item of slot `slot` (ItemType 0..4); field 0..19 = Item::stats[i].
// G2: fields 20..22 = nameIndex[0], nameIndex[1], label (Item+0x88/+0x8c/+0x90) for the
// page-side GenerateName text composition (Item::GenerateName @0x4c35c).
EMSCRIPTEN_KEEPALIVE int dr_item(int slot, int field)
{
    Game *g = G();
    if (!g || slot < 0 || slot >= static_cast<int>(g->items.size())) return 0;
    const Item &it = *g->items[slot];
    if (field < 0) return it.id;
    if (field < ITEM_STAT_SLOTS) return it.stats[field];
    if (field == 20) return it.nameIndex[0];
    if (field == 21) return it.nameIndex[1];
    if (field == 22) return it.label;
    return 0;
}

// Owned skill of slot `slot` (0..3): 0 type, 1 rank, 2 charge, 3 maxCharge, 4 ready.
EMSCRIPTEN_KEEPALIVE int dr_skill(int slot, int field)
{
    Game *g = G();
    if (!g || slot < 0 || slot >= static_cast<int>(g->skills.size())) return 0;
    const Skill &s = *g->skills[slot];
    switch (field) {
    case 0: return s.type;
    case 1: return s.rank;
    case 2: return s.charge;
    case 3: return s.maxCharge;
    case 4: return s.IsReady() ? 1 : 0;
    default: return 0;
    }
}

// ---- play --------------------------------------------------------------------------
// packed: len bytes of col + row*6 (0..35) in draw order. Returns 1 if the turn ran.
EMSCRIPTEN_KEEPALIVE int dr_play(const uint8_t *packed, int len)
{
    Game *g = G();
    if (!g || !packed || len <= 0) return 0;
    std::vector<Game::Cell> path;
    path.reserve(static_cast<size_t>(len));
    for (int i = 0; i < len; ++i) {
        const int v = packed[i];
        path.push_back(Game::Cell{v % BOARD_SIZE, v / BOARD_SIZE});
    }
    // M9 (task A3): one ordered event log per action — start clean, record the chain tiles in
    // draw order (read-only board reads), then run the turn.
    g_web.ClearEvents();
    for (const Game::Cell &c : path) {
        const Tile &t = g->At(c.col, c.row);
        g_web.Ev(EV_CHAIN_TILE, c.col, c.row, static_cast<int>(t.type));
    }
    return g->PlayMove(path) ? 1 : 0;
}

// 1 if the chain drawn so far (packed cell indices) could still be part of a legal move: the
// rebuild's own linking rules (Game::IsValidPrefix -> CanMatchType @ 0x645ac), no length rule.
EMSCRIPTEN_KEEPALIVE int dr_path_ok(const uint8_t *packed, int len)
{
    Game *g = G();
    if (!g || !packed || len <= 0) return 0;
    std::vector<Game::Cell> path;
    for (int i = 0; i < len; ++i) path.push_back(Game::Cell{packed[i] % BOARD_SIZE, packed[i] / BOARD_SIZE});
    return g->IsValidPrefix(path) ? 1 : 0;
}

// ---- menus -------------------------------------------------------------------------
// pending menu kind (MENU_LEVEL_UP/SHOP/UPGRADE) or -1 when none.
EMSCRIPTEN_KEEPALIVE int dr_menu()
{
    Game *g = G();
    return (g && g->menuPending) ? g->menuType : -1;
}

EMSCRIPTEN_KEEPALIVE int dr_menu_offer(int i, int field)
{
    Game *g = G();
    if (!g || !g->menuPending) return -1;

    if (g->menuType == MENU_LEVEL_UP) {
        if (i < 0 || i > 3 || !g->menuLevelSkills[i]) return -1;
        const Skill &s = *g->menuLevelSkills[i];
        const bool stat = s.type >= STAT_SKILL_BASE;
        if (field == 0) return stat ? 4 : 2;
        if (field == 1) return s.type;
        if (stat) {
            switch (field) {
            case 5:  return s.statHp;
            case 6:  return s.statHeal;
            case 7:  return s.statArmour;
            case 8:  return s.statDamage;
            case 9:  return s.statLuckChance;
            case 10: return s.statVitChance;
            case 11: return s.statStrChance;
            case 12: return s.statDexChance;
            default: return 0;
            }
        }
        switch (field) {
        case 2: return s.rank;
        case 3: return s.maxCharge;
        case 4: return s.charge;
    default: return 0;
    }
}

    if (g->menuType == MENU_SHOP) {
        if (i < 0 || i > 2) return -1;
        if (g->menuShopSkillSlot == i && g->menuShopSkill) {
            const Skill &s = *g->menuShopSkill;
            if (field == 0) return 2;
            if (field == 1) return s.type;
            switch (field) {
            case 2: return s.rank;
            case 3: return s.maxCharge;
            case 4: return s.charge;
            // U13b: Skill+0x80 `active` — the shop band's "Replaces:" item for a SKILL row is
            // Game::GetItem(row +0x80) (FoldoutMenu::HighlightButton @0x40cb0:66-68, Render
            // @0x40eb4:70-79): the skill object's +0x80 = active (skill.h:13), so the band
            // shows the equipped item of type (int)active (0 weapon / 1 helmet).
            case 5: return s.active ? 1 : 0;
            default: return 0;
            }
        }
        const Item *it = g->menuShopItems[i].get();
        if (!it) return -1;
        if (field == 0) return 1;
        if (field == 1) return static_cast<int>(it->type);
        if (field == 2) return it->id;
        if (field == 3) return it->label;
        // U8b: GenerateName indices (Item+0x88/+0x8c) for the shop-row title
        // (Item::GenerateName @0x4c35c writes both; the page composes "%s  %s").
        if (field == 4) return it->nameIndex[0];
        if (field == 5) return it->nameIndex[1];
        if (field >= 10 && field < 10 + ITEM_STAT_SLOTS) return it->stats[field - 10];
        return 0;
    }

    if (g->menuType == MENU_UPGRADE) {
        if (i < 0 || i > 3) return -1;
        if (field == 0) return 3;
        if (field == 1) return static_cast<int>(g->menuUpTypes[i]);
        if (field == 2) return g->menuUpgrades[i].value;
        if (field == 3) return g->menuUpgrades[i].stat;
        // Y3/K3: the row shows the EQUIPPED item of that type (FoldoutMenu::Initialise
        // @0x42794:1005 Game::GetItem(type) -> row item +0x58); its icon = the drawn id
        // (Item+0x84, Item::Initialise @0x4c0ec). field 4 exposes it for the page icon.
        if (field == 4) { const Item *eq = g->items[g->menuUpTypes[i]].get(); return eq ? eq->id : 0; }
        return 0;
    }
    return -1;
}

EMSCRIPTEN_KEEPALIVE void dr_choose(int a, int b)
{
    Game *g = G();
    if (!g) return;
    g_web.ClearEvents();   // M9 (task A3): the menu choice is its own action (deferred attack runs here)
    g->ChooseMenu(a, b);
}

// ---- end / save --------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int dr_over()
{
    Game *g = G();
    return (g && g->gameOver) ? 1 : 0;
}

// A21: the GameOverScreen::Initialise(bool) argument of CheckEndGame @0002f960:54-57 —
// true = survived the sprint challenge (achievement 0x4c queued there), false = killed.
// Render @000454f0:21-26 then draws quad +0xf0 ("YOU ESCAPED!", green dim) or +0x7c
// ("YOU WERE KILLED", red dim — colour set in Initialise @000457d4:38-51).
// Condition mirrors the engine's own victory test (CheckEndGame in combat.cpp:235).
EMSCRIPTEN_KEEPALIVE int dr_escaped()
{
    Game *g = G();
    return (g && g->gameOver && g->survivedSprint && g->challenge &&
            g->mode == MODE_SPRINT) ? 1 : 0;
}

// Serialise into an internal buffer; pointer valid until the next dr_save/dr_buf call.
// A23: game over KILLS the save — CheckEndGame @0x2f960:156-157 sets the save size (+0x14308)
// to 0 and the changed flag (+0x1430c) to 1, and the Java loop (DungeonRaidSurfaceView.java:
// 385-389) sees size == 0 and DELETES the save file (file.delete()) instead of writing it.
// So dr_save reports size 0 once the run is over; the web layer deletes the stored save.
EMSCRIPTEN_KEEPALIVE uint8_t *dr_save()
{
    Game *g = G();
    if (!g) { g_save.clear(); return nullptr; }
    if (g->gameOver) { g_save.clear(); return nullptr; }   // +0x14308 = 0 on game over
    g_save = g->SerialiseState();
    return g_save.empty() ? nullptr : g_save.data();
}

EMSCRIPTEN_KEEPALIVE int dr_save_len() { return static_cast<int>(g_save.size()); }

// Reusable staging buffer JS fills before dr_load (avoids exporting malloc).
EMSCRIPTEN_KEEPALIVE uint8_t *dr_buf(int size)
{
    if (size < 1) size = 1;
    g_stage.assign(static_cast<size_t>(size), 0);
    return g_stage.data();
}

// Load a save written by dr_save. Mirrors tests/sim.cpp: fresh game first (InitNewGame
// builds items), then DeserialiseState overwrites everything the save carries.
// W9: the classes blob goes in first — the original loads it in Game::Start @0x260ac:174-176,
// long before any run, so Continue must not drop the session's class meta either.
EMSCRIPTEN_KEEPALIVE int dr_load(const uint8_t *bytes, int len)
{
    if (!bytes || len <= 0) return 0;
    CarryClasses();   // W9: keep the outgoing object's class meta first (see above)
    auto next = std::make_unique<Game>(&g_web);
    if (!g_classes.empty()) next->DeserialiseClasses(g_classes);
    next->seedOverride = 1;
    next->NewGame(DIFF_NORMAL);
    if (!next->DeserialiseState(std::vector<uint8_t>(bytes, bytes + len))) return 0;
    g_web.sounds.clear();
    g_web.helps.clear();
    g_web.labels.clear();
    g_web.ClearEvents();   // M9 (task A3): a new run starts the event log clean
    g_game = std::move(next);
    g_scored = false;
    g_preview.clear();
    return 1;
}

// ==== M8 API (task W3) ===========================================================
// Game rules live in rebuild/src/m8.cpp (every function there cites its original); this file
// only packs/unpacks.

// ---- starting a game ---------------------------------------------------------------
// seed, difficulty 0..3, mode 3 Classic / 0 Dungeon Sprint / 1 Pretzel Hero, klass 0..9
// (locked picks fall back to the Adventurer, ClassScreen::FadeOut @ 0x3730c).
// W9: the run starts on the ONE session Game (dr_init), like the original — Game is built at
// app start (ctor + Game::Start @0x260ac) and NewGame @0x2de68 / StartChallenge @0x2de10 just
// re-run InitNewGame on it, so class meta and the carried name stay on the object instead of
// being reloaded from a blob. StartGame (src/m8.cpp) re-seeds every RNG stream first, so the
// W6 ordering still holds: dr_name_random runs BEFORE this call and is wiped by StartGame.
EMSCRIPTEN_KEEPALIVE void dr_new_game(uint32_t seed, int difficulty, int mode, int klass)
{
    dr_init();                        // create the object if the module never booted it
    g_web.sounds.clear();
    g_web.helps.clear();
    g_web.labels.clear();
    g_web.ClearEvents();   // M9 (task A3): a new run starts the event log clean
    // W9: the object is reused, so the two things InitNewGame does NOT clear go back to their
    // fresh-Game values here — the original's screen flow has already put them there by the
    // time NewGame can run:
    //   gameOver (+0xce64, GameOverScreen shown) is cleared ONLY by Game::BackPressed
    //     @0x26a98:35 (game-over -> main menu), the only route to "New game";
    //   menuPending/attackPending/endOfTurnPending model an open FoldoutMenu (open flag
    //     Interface+0xdf8, set by FoldoutMenu::Initialise @0x42794): a reward menu freezes
    //     ProcessInput until the pick, so no run can start with one open in the original —
    //     the page can leave the screen with it open (menuBtn), so it is closed here.
    g_game->gameOver = false;
    g_game->menuPending = false;
    g_game->attackPending = false;
    g_game->endOfTurnPending = false;
    g_game->playerName = g_name;      // W6: Game+0xc1f8 carries across games (see g_name)
    StartGame(*g_game, seed, difficulty, mode, klass);   // src/m8.cpp
    if (!g_game->classesSave.empty()) g_classes = g_game->classesSave;
    g_scored = false;
    g_preview.clear();
}

// Game+0x14330 (GameMode 3/0/1, Game::GetModeName @ 0x24978) / Game+0x14328 (Difficulty).
EMSCRIPTEN_KEEPALIVE int dr_mode() { Game *g = G(); return g ? static_cast<int>(g->mode) : MODE_CLASSIC; }
EMSCRIPTEN_KEEPALIVE int dr_difficulty() { Game *g = G(); return g ? static_cast<int>(g->difficulty) : DIFF_NORMAL; }

// ---- wall clock (task W7) ---------------------------------------------------------------
// Game::IsClockEnabled (reads Game+0x142f9, JNI wrapper @ 0x234dc) / Game::SetClockState
// @ 0x25038 (+0x142f9 on, +0x142fc = old ^ new; src/m8.cpp). The clock string is the device's
// local time formatted "h:mm a" Java-side (DungeonRaidRenderer.onDrawFrame) — the page draws it.
EMSCRIPTEN_KEEPALIVE int dr_clock_enabled() { Game *g = G(); return g && ClockEnabled(*g) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void dr_set_clock(int on) { if (G()) SetClockState(*g_game, on != 0); }

// ---- settings toggles + help queue (task A24) ----------------------------------------
// Game::SetHelpState @0x2505c (+0x142f8), SetAttackDimmingState @0x25010 (+0x142fa),
// SetHighFramerate @0x24fe8 (+0x142fb) — GameMenu::Process @0x44564:69-85 toggles them.
EMSCRIPTEN_KEEPALIVE int dr_help_enabled() { Game *g = G(); return g && g->helpEnabled ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void dr_set_help(int on) { if (G()) g_game->helpEnabled = on != 0; }
EMSCRIPTEN_KEEPALIVE int dr_dimming_enabled() { Game *g = G(); return g && g->attackDimming ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void dr_set_dimming(int on) { if (G()) g_game->attackDimming = on != 0; }
EMSCRIPTEN_KEEPALIVE int dr_smooth_enabled() { Game *g = G(); return g && g->highFramerate ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void dr_set_smooth(int on) { if (G()) g_game->highFramerate = on != 0; }
// HelpMenu+0x38 seen bitmask (bit N set = tip N still allowed; HelpMenu::Initialise
// @0x48348:15-18 tests + clears). Serialised inside the save (HelpMenu::Serialise @0x474a0,
// Game::SerialiseState @0x265f4:131) — the page reads/writes it here so the bits travel
// with the save, like the original (D13 5.7).
EMSCRIPTEN_KEEPALIVE int dr_help_state() { Game *g = G(); return g ? g->helpState : 0; }
EMSCRIPTEN_KEEPALIVE void dr_set_help_state(int bits) { if (G()) g_game->helpState = bits; }
// HelpMenu::Initialise call sites (fx().ShowHelp) recorded for the page: one entry per
// (N, tile) request since the last dr_help_clear. F5: dr_help_cell packs the anchor cell
// (col+1)*8+(row+1) — 0 = no tile (the Interface::ShowHelp* fixed anchors, page-side table).
EMSCRIPTEN_KEEPALIVE int dr_help_count() { return static_cast<int>(g_web.helps.size()); }
EMSCRIPTEN_KEEPALIVE int dr_help_get(int i)
{ return (i >= 0 && i < static_cast<int>(g_web.helps.size())) ? g_web.helps[static_cast<size_t>(i)].n : -1; }
EMSCRIPTEN_KEEPALIVE int dr_help_cell(int i)
{
    if (i < 0 || i >= static_cast<int>(g_web.helps.size())) return -1;
    const auto &h = g_web.helps[static_cast<size_t>(i)];
    return h.col < 0 ? 0 : (h.col + 1) * 8 + (h.row + 1);
}
EMSCRIPTEN_KEEPALIVE void dr_help_clear() { g_web.helps.clear(); }

// ---- classes ------------------------------------------------------------------------
// field: 0 level (0 = locked), 1 race, 2 inherent race, 3-6 perk slots (255 = empty),
// 7-10 flaw slots, 11 perk/flaw slots unlocked (count of IsPerkUnlocked @ 0x35d78),
// 12 replacement skills allowed (GetReplacementSkillsAllowed @ 0x35dc0), 13 race choice unlocked
// (Game::IsRaceUnlocked @ 0x248a0), 14-33 the 20 base skills, 34-53 the 20 replacement slots,
// 54-58 the per-class counters CharacterClass +0x58..+0x68: Games Played, Enemies Killed,
// Highest Normal / Hard / Harder Score (CheckEndGame @ 0x2f960:61-63, :79-91).
// -1 = bad klass or field.
EMSCRIPTEN_KEEPALIVE int dr_class(int klass, int field)
{
    Game *g = G();
    return g ? ClassGet(*g, klass, field) : -1;
}

// what: 0 perk, 1 flaw, 2 race, 3 replacement skill -> 1 applied / 0 refused.
// REVIEW TOOL (not game logic; user request at the M8 checkpoint): raise a class to `level` with the original's own
// Game::SetClassLevel @ 0x24d28 (raise-only, as the trophy/unlock path and tools/emu select_class use it), so a reviewer
// can see locked classes and the level-5/10 customisation stages without earning trophies first.
EMSCRIPTEN_KEEPALIVE int dr_review_class_level(int klass, int level)
{
    Game *g = G();
    if (!g || klass < 0 || klass >= CLASS_COUNT || level < 1 || level > 10) return 0;
    g->SetClassLevel(static_cast<ClassType>(klass), level);
    return 1;
}

// NOTE for the screens: dr_meta_save runs SerialiseClasses, which validates
// (SerialiseFullState @ 0x36494) and would drop a perk with no matching flaw — save on leaving
// the class editor (the ClassScreen::FadeOut @ 0x3730c moment), not after every slot.
EMSCRIPTEN_KEEPALIVE int dr_class_set(int klass, int what, int slot, int value)
{
    Game *g = G();
    return (g && ClassSet(*g, klass, what, slot, value)) ? 1 : 0;
}

// Pops the oldest unlock popup: field 0 = ClassType, then field 1 = level (same popup);
// -1 when none / no level read. (Game::unlockPopups, ShowUnlockMenu @ 0x671ec.)
EMSCRIPTEN_KEEPALIVE int dr_unlock_next(int field)
{
    Game *g = G();
    return g ? PopUnlock(*g, field) : -1;
}

// ---- meta persistence (localStorage) ----------------------------------------------
// Container: 'D','R','M','1' | u32 classesLen | the original all-10-classes blob
// (Game::SerialiseClasses @ 0x24d98: 0x00 0x0a + 10 x 47 B) | u32 scoresLen | ScoreBook blob.
// Pointer valid until the next dr_meta_save/dr_save/dr_buf call.
EMSCRIPTEN_KEEPALIVE uint8_t *dr_meta_save()
{
    Game *g = G();
    g_meta.clear();
    std::vector<uint8_t> cls;
    if (g) {
        g->SerialiseClasses();                 // Validate + 10 x SerialiseFullState @ 0x36494
        cls = g->classesSave;
        if (!cls.empty()) g_classes = cls;
    }
    const std::vector<uint8_t> sc = g_scores.Serialise();
    g_meta.push_back('D'); g_meta.push_back('R'); g_meta.push_back('M'); g_meta.push_back('1');
    Put32(g_meta, static_cast<uint32_t>(cls.size()));
    g_meta.insert(g_meta.end(), cls.begin(), cls.end());
    Put32(g_meta, static_cast<uint32_t>(sc.size()));
    g_meta.insert(g_meta.end(), sc.begin(), sc.end());
    return g_meta.data();
}
EMSCRIPTEN_KEEPALIVE int dr_meta_save_len() { return static_cast<int>(g_meta.size()); }

EMSCRIPTEN_KEEPALIVE int dr_meta_load(const uint8_t *bytes, int len)
{
    if (!bytes || len < 12) return 0;
    if (bytes[0] != 'D' || bytes[1] != 'R' || bytes[2] != 'M' || bytes[3] != '1') return 0;
    int off = 4;
    const int clsLen = Get32(bytes + off); off += 4;
    if (clsLen < 0 || off + clsLen + 4 > len) return 0;
    const uint8_t *cls = bytes + off; off += clsLen;
    const int scLen = Get32(bytes + off); off += 4;
    if (scLen < 0 || off + scLen > len) return 0;
    int ok = 1;
    if (clsLen > 0) {
        g_classes.assign(cls, cls + clsLen);
        Game *g = G();
        // W9: +0x14314 now holds the loaded blob too (Game::Start @0x260ac:174-176 reads it as
        // "classes present"), so CarryClasses() can never roll the classes back to an older blob.
        if (g) {
            g->classesSave = g_classes;
            // Game::DeserialiseClasses @ 0x24d54: header byte 1 must be 10, else untouched.
            if (!g->DeserialiseClasses(g_classes)) ok = 0;
        }
    }
    if (scLen > 0 && !g_scores.Deserialise(bytes + off, scLen)) ok = 0;
    return ok;
}

// ---- in a game ----------------------------------------------------------------------
// 1 when Interface::ProcessInput @ 0x49738:84-97 would run it (CanActivate @ 0x60204 gate);
// the activation itself may open a reward menu (dr_menu).
EMSCRIPTEN_KEEPALIVE int dr_skill_activate(int slot)
{
    Game *g = G();
    if (!g) return 0;
    g_web.ClearEvents();   // M9 (task A3): the skill activation is its own action
    return TryActivateSkill(*g, slot) ? 1 : 0;
}

// 1 when the tap was a trophy collection (Game::TapTile, ProcessInput @ 0x330c0:325-389).
EMSCRIPTEN_KEEPALIVE int dr_tap(int col, int row)
{
    Game *g = G();
    if (!g) return 0;
    g_web.ClearEvents();   // M9 (task A3): the trophy tap is its own action
    return g->TapTile(col, row) ? 1 : 0;
}

// Stage a chain for the drag preview; returns the chain attack the original shows at +0xc23c
// (ProcessTileSelection @ 0x2c8dc). Writes nothing: no game state, no RNG stream.
EMSCRIPTEN_KEEPALIVE int dr_preview(const uint8_t *packed, int len)
{
    Game *g = G();
    g_preview = Unpack(packed, len);
    if (!g || g_preview.empty()) return 0;
    return PreviewChainAttack(*g, g_preview);
}

// packed for the tile staged by dr_preview: bits 0-1 damage type (Tile::GetDamageType @ 0x644bc),
// bit 2 will-die cross (UpdateCrossVisibility @ 0x2c5e8), bits 8-15 damage label value.
// Label value is 0: UpdateCrossVisibility creates the two preview labels with value 0
// (:84-85 type 0x23, :116-117 type 0x1e) and SimpleLabel::UseNumberFront @ 0x5ebec /
// UseNumberBack @ 0x5ec08 never draw a number for those types. UNKNOWN(displayed damage number:
// the original's per-enemy preview label is an icon, not a number).
EMSCRIPTEN_KEEPALIVE int dr_preview_tile(int col, int row)
{
    Game *g = G();
    if (!g || g_preview.empty()) return 0;
    int p = PreviewDamageType(*g, g_preview, col, row) & 3;
    if (PreviewWillDie(*g, g_preview, col, row)) p |= 4;
    return p;   // label value (bits 8-15) = 0, see above
}

// ---- player name (W6; NameScreen) -----------------------------------------------------------
// The name is Game+0xc1f8 (max 16 chars, truncate-not-refuse — ProcessInput @0x57a10:498-501 /
// JNI SetName @0x23de0) and is carried across dr_new_game calls (it persists in the original;
// InitNewGame never clears it) and stored in the run save (SerialiseState @0x265f4:114).

// dr_name_random() — NameScreen::GenerateRandomName @0x5615c (tables ELF 0x5da04/0x5db00,
// formats "%s%s"/"%s%s %s%s"; RandI draws on stream 0, RNG.md). Sets the carried name and
// returns it. RNG note: in the original the generator runs between the two InitNewGame calls
// (NewGame @0x2de68 seeds all streams; the Play button then runs InitNewGame again, which
// re-seeds everything) — so call this BEFORE dr_new_game; pre-game draws are wiped by
// StartGame's SetRandSeed(..., RAND_ALL)+FlushRandSeeds and cannot shift the game RNG
// (proved with diff.py, tasks/W6/RESULT.md).
EMSCRIPTEN_KEEPALIVE const char *dr_name_random()
{
    // W8/W9: RandI is global state (src/rng.cpp:42) and GenerateRandomName only needs a Game to
    // store into (src/m8.cpp:294) — since dr_init() runs at module start that is the session
    // Game; the scratch branch only covers a module used without it. Either way the draws are
    // wiped by dr_new_game -> StartGame's SetRandSeed(..., RAND_ALL) (W6 proof), so call this
    // BEFORE dr_new_game.
    if (g_game) g_name = GenerateRandomName(*g_game);
    else { Game tmp(&g_web); g_name = GenerateRandomName(tmp); }
    return g_name.c_str();
}

// dr_name_set(s) — the Play-button copy (ProcessInput @0x57a10:498-501): cap 16 chars,
// never refuse. Returns 1.
EMSCRIPTEN_KEEPALIVE int dr_name_set(const char *s)
{
    // W8: works with no live game too (scratch Game, same 16-char truncation).
    if (g_game) { SetPlayerName(*g_game, s); g_name = g_game->playerName; }
    else { Game tmp(&g_web); SetPlayerName(tmp, s); g_name = tmp.playerName; }
    return 1;
}

// dr_name() — the carried name (g_name = Game+0xc1f8, W6). Read-only pack for the screens:
// prefill the new-game name field (NameScreen::Show @0x589a0:61 strncpy of the current name)
// and the game-over name label (GameOverScreen::Initialise @0x457d4:84 label[0] = Game+0xc1f8).
EMSCRIPTEN_KEEPALIVE const char *dr_name()
{
    return g_name.c_str();
}

// ---- scores and lifetime stats ------------------------------------------------------
// Page = ScoreType 0..5 Easy/Normal/Hard/Harder/Sprint/Pretzel (the GameMode overload
// AddScore @ 0x5ce88 adds 4, so challenge runs land on 4/5). Returns the 0-based rank or -1;
// gated on game over like CheckEndGame @ 0x2f960:27 (one insert per run).
EMSCRIPTEN_KEEPALIVE int dr_score_add()
{
    Game *g = G();
    if (!g || !g->gameOver || g_scored) return -1;
    const int page = !g->challenge ? static_cast<int>(g->difficulty)
                                   : (g->mode == MODE_PRETZEL ? ScoreType::SCORE_PRETZEL
                                                              : ScoreType::SCORE_SPRINT);
    const int rank = g_scores.Add(*g, page);
    if (rank >= 0) g_scored = true;
    return rank;
}

// dr_score(page, rank, field) — field is the ScoreScreen entry (0x38 B, AddScore @ 0x5cc5c):
//   0 score   (entry+0x24, param_6 = the sort key; -1 = empty slot)
//   1 class   REBUILD-ADDED (screens only): Game+0x15cd4 at insert time (AddScore has no class
//             argument — the original stores the player NAME at entry+0x00, see dr_score_name)
//   2 difficulty = the ScoreType the entry was filed under (page 0..5; AddScore stores it only
//             as the page index — difficulty 0..3, SCORE_SPRINT/SCORE_PRETZEL 4/5)
//   3 turn    (entry+0x14, param_2 = Game+0xc)
//   4..6 mode columns (entry+0x18/0x1c/0x20, param_3..5): classic = XP / Gold / Upgrade total
//             (+0xc170+0xc210, +0xc178+0xc214, +0xc174+0xc218); sprint = the same /4
//             (CheckEndGame @ 0x2f960:110-125); pretzel = Avg +0x15e64 / Max +0x15e68 /
//             Chains +0x15e60 (:94-103)
//   7..10 the 4 skill ids (entry+0x28..+0x34, ScoreScreen::SetSkills @ 0x5c398)
// Returns -1 for an empty slot / bad argument.
EMSCRIPTEN_KEEPALIVE int dr_score(int page, int rank, int field)
{
    const ScoreEntry *e = g_scores.Get(page, rank);
    if (!e) return -1;
    switch (field) {
    case 0: return e->score;
    case 1: return e->cls;
    case 2: return page;
    case 3: return e->turn;
    case 4: case 5: case 6: return e->col[field - 4];
    case 7: case 8: case 9: case 10: return e->skills[field - 7];
    default: return -1;
    }
}

// dr_score_name(page, rank) — the player name at entry+0x00 (AddScore @0x5cc5c:96 strcpy of
// Game+0xc1f8; shown by InitialisePage @0x5d0e0:146). Returns a pointer into a static buffer
// (valid until the next call) or NULL for an empty slot / bad argument. cwrap 'string' return.
EMSCRIPTEN_KEEPALIVE const char *dr_score_name(int page, int rank)
{
    static char buf[21];
    const ScoreEntry *e = g_scores.Get(page, rank);
    if (!e) return nullptr;
    std::memcpy(buf, e->name, 20);
    buf[20] = '\0';
    return buf;
}

// The StatsMenu's 15 tracked stats (StatsMenu::CreateLabels @ 0x61178 / Show @ 0x614bc;
// order = the F1 MODES_SCREENS.md list used by template.html STAT_LABELS). SetLabelValue
// @ 0x60f08 displays param_2 + param_3, capped at 100 for the percentage rows (param_4).
// UNKNOWN(name, value): Game+0xc1a8/0xb0/0xb8/0xc0 have no reader in analysis/decomp
// (FIELDS.md; InitNewGame sets them to 1) — they are read here exactly as Show does.
EMSCRIPTEN_KEEPALIVE int dr_lifetime(int id)
{
    Game *g = G();
    if (!g) return 0;
    auto cap = [](int v) { return v > 100 ? 100 : v; };
    switch (id) {
    case 0:  return g->baseDamage + g->attackBonus;                    // Show :76 (+0xc180+c184)
    case 1:  return g->unknownC1A8[0] + g->boostStrCount;              // :74 (+0xc1a8+c1ac)
    case 2:  return g->unknownC1A8[1] + g->boostDexCount;              // :80 (+0xc1b0+c1b4)
    case 3:  return g->unknownC1A8[2] + g->boostVitCount;              // :86 (+0xc1b8+c1bc)
    case 4:  return g->unknownC1A8[3] + g->boostLukCount;              // :92 (+0xc1c0+c1c4)
    case 5:  return g->armourBase + g->armourBonus;                    // :82 (+0xc1d0+c1d4)
    case 6:  return g->swordDamage;                                    // :98 (+0xc17c)
    case 7:  return cap(g->critChance);                                // :99 0x32+(+0xc18c-0x32)
    case 8:  return 100 - (g->armourBreakChance > 100 ? 100 : g->armourBreakChance);  // :101-105
    case 9:  return g->baseMaxHp + g->bonusMaxHp;                      // :88 (+0xc15c+c160)
    case 10: return g->healBase + g->healBonus;                        // :94 (+0xc1c8+c1cc)
    case 11: return cap(g->bonusChance[TILE_SKULL][0] + g->boostStrChance);   // :78 (+0xc1e8+c1ec)
    case 12: return cap(g->bonusChance[TILE_COIN][0] + g->boostLukChance);    // :96 (+0xc1d8+c1dc)
    case 13: return cap(g->bonusChance[TILE_SHIELD][0] + g->boostDexChance);  // :84 (+0xc1f0+c1f4)
    case 14: return cap(g->bonusChance[TILE_POTION][0] + g->boostVitChance);  // :90 (+0xc1e0+c1e4)
    default: return 0;
    }
}

// F5b: the three ARGUMENTS StatsMenu::SetLabelValue(int, int, int, bool) @ 0x60f08 gets per row,
// so the page can run the label-colour test @ 0x60f08:23-36 and :42-51 itself:
//     green (0,255,0) iff param_3 >= 1 AND (!param_4 || param_2 < 100), else white (255,255,255)
// (the green branch writes label bytes +0x40..+0x42 = 00 ff 00, the white branch ff ff ff;
// +0x43 = 0 in both). The base/bonus/pct triples below are exactly the SetLabelValue calls in
// StatsMenu::Show @ 0x614bc:73-105 — the same Game fields dr_lifetime adds up for the text
// (field 0 = base param_2, 1 = bonus param_3, 2 = pct param_4; anything else returns 0).
// Read-only: no game state is written here.
EMSCRIPTEN_KEEPALIVE int dr_stat_row(int row, int field)
{
    Game *g = G();
    if (!g) return 0;
    int base = 0, bonus = 0, pct = 0;
    switch (row) {
    case 0:  base = g->level; break;                                      // Show :73  +0xc20c
    case 1:  base = g->unknownC1A8[0]; bonus = g->boostStrCount; break;   // :74  +0xc1a8 + +0xc1ac
    case 2:  base = g->baseDamage; bonus = g->attackBonus; break;         // :76  +0xc180 + +0xc184
    case 3:  base = g->bonusChance[TILE_SKULL][0]; bonus = g->boostStrChance; pct = 1; break;  // :78 +0xc1e8 + +0xc1ec
    case 4:  base = g->unknownC1A8[1]; bonus = g->boostDexCount; break;   // :80  +0xc1b0 + +0xc1b4
    case 5:  base = g->armourBase; bonus = g->armourBonus; break;         // :82  +0xc1d0 + +0xc1d4
    case 6:  base = g->bonusChance[TILE_SHIELD][0]; bonus = g->boostDexChance; pct = 1; break; // :84 +0xc1f0 + +0xc1f4
    case 7:  base = g->unknownC1A8[2]; bonus = g->boostVitCount; break;   // :86  +0xc1b8 + +0xc1bc
    case 8:  base = g->baseMaxHp; bonus = g->bonusMaxHp; break;           // :88  +0xc15c + +0xc160
    case 9:  base = g->bonusChance[TILE_POTION][0]; bonus = g->boostVitChance; pct = 1; break; // :90 +0xc1e0 + +0xc1e4
    case 10: base = g->unknownC1A8[3]; bonus = g->boostLukCount; break;   // :92  +0xc1c0 + +0xc1c4
    case 11: base = g->healBase; bonus = g->healBonus; break;             // :94  +0xc1c8 + +0xc1cc
    case 12: base = g->bonusChance[TILE_COIN][0]; bonus = g->boostLukChance; pct = 1; break;   // :96 +0xc1d8 + +0xc1dc
    case 13: base = g->swordDamage; break;                                // :98  +0xc17c (bonus 0)
    case 14: base = 0x32; bonus = g->critChance - 0x32; pct = 1; break;   // :99  0x32 + (+0xc18c - 0x32)
    case 15: base = g->maxArmour; break;                                  // :100 +0xc168 (bonus 0)
    case 16: base = 0x32;                                                 // :101-105 0x32 + (0x32 - clamp(+0xc188,100))
             bonus = 0x32 - (g->armourBreakChance > 100 ? 100 : g->armourBreakChance); pct = 1; break;
    default: return 0;
    }
    switch (field) {
    case 0: return base;
    case 1: return bonus;
    case 2: return pct;
    default: return 0;
    }
}

// ---- platform ----------------------------------------------------------------------
// Pop the next queued sound id (Sound enum), or -1. Art build only needs this.
EMSCRIPTEN_KEEPALIVE int dr_poll_sound()
{
    if (g_web.sounds.empty()) return -1;
    const int s = g_web.sounds.front();
    g_web.sounds.erase(g_web.sounds.begin());
    return s;
}

// Floating labels queued by the last action (Game::AddSimpleLabel @0x28744/@0x2d0d4, HUD.md §3).
// Unpacks the Platform::AddLabel hook for the demo to draw — presentation plumbing, no game logic.
// field: 0 SimpleLabelType, 1 col (-1 = state-anchored: health bar / poison icon), 2 row, 3 value,
// 4 tile type of the source tile (A4b: the type-0 match label is icon-only — the icon is the
// matched tile's own icon, AddSimpleLabel(Tile*,TileType) @0x28744 colours it by the tile).
EMSCRIPTEN_KEEPALIVE int dr_label_count() { return static_cast<int>(g_web.labels.size()); }
EMSCRIPTEN_KEEPALIVE int dr_label(int i, int field)
{
    if (i < 0 || i >= static_cast<int>(g_web.labels.size())) return 0;
    const WebPlatform::Label &l = g_web.labels[static_cast<size_t>(i)];
    switch (field) {
    case 0: return l.kind;
    case 1: return l.col;
    case 2: return l.row;
    case 3: return l.value;
    case 4: return l.tileType;
    default: return 0;
    }
}
EMSCRIPTEN_KEEPALIVE void dr_label_clear() { g_web.labels.clear(); }

// ---- M9 event log (task A3) -----------------------------------------------------------
// One ordered event log per action (dr_play / dr_skill_activate / dr_tap / dr_choose):
// cleared at the start of each action, read after it. Layout (milestones/M8/API_CONTRACT.md
// "M9 events"): dr_event(i, field) — field 0 = kind, 1 = phase (0 chain/match, 1 enemy
// attack), 2..7 = payload per kind. Pure presentation: recording draws no RNG and changes
// no game state (the base AddLabel/AddParticles/AddFlare stream-0 draws are kept).
EMSCRIPTEN_KEEPALIVE int dr_events_count() { return static_cast<int>(g_web.events.size()); }
EMSCRIPTEN_KEEPALIVE int dr_event(int i, int field)
{
    if (i < 0 || i >= static_cast<int>(g_web.events.size()) ||
        field < 0 || field >= EVENT_FIELDS) return 0;
    return g_web.events[static_cast<size_t>(i)][static_cast<size_t>(field)];
}
EMSCRIPTEN_KEEPALIVE void dr_events_clear() { g_web.ClearEvents(); }

// emcc links a JS module with a main; keep it trivial (C++ linkage, not extern "C").
// W9: the module starts with the Game in place (Game ctor + Game::Start @0x260ac at app start),
// so dr_class / dr_lifetime / dr_meta_* answer before the page has started a run.
} // extern "C"

int main() { dr_init(); return 0; }
