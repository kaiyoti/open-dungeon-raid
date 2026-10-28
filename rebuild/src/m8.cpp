// M8 game-side helpers (task W3): drag preview, class edit gates, session start, high scores.
// Every rule is ported from a decompiled original (addresses = Ghidra base, ELF VMA + 0x10000);
// nothing here is invented — UNKNOWN(...) marks what the decomp cannot show.
#include "dr/m8.h"
#include "dr/character_class.h"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <list>

namespace dr {

namespace {

int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Temporarily installs `path` as Game::chain so the original's chain walkers (ComputeChainAttack,
// ChainDamageType/Tile::GetDamageType) can run on a preview chain that was never drawn. Same
// save/restore pattern Game::IsValidPrefix (turn.cpp:109-127) already uses. Only `chain` is
// touched — no Tile byte, no RNG stream, no menu/state field.
struct PreviewScope {
    Game &g;
    std::list<Tile *> saved;
    PreviewScope(Game &game, const std::vector<Game::Cell> &path) : g(game)
    {
        saved.swap(g.chain);
        for (const Game::Cell &c : path)
            if (c.col >= 0 && c.col < BOARD_SIZE && c.row >= 0 && c.row < BOARD_SIZE)
                g.chain.push_back(&g.At(c.col, c.row));
    }
    ~PreviewScope() { g.chain.swap(saved); }
};

bool InBoard(int col, int row)
{
    return col >= 0 && col < BOARD_SIZE && row >= 0 && row < BOARD_SIZE;
}

} // namespace

// ---------------------------------------------------------------------------
// Drag preview.
// ---------------------------------------------------------------------------

// ProcessTileSelection @ 0x2c8dc (+0xc23c): the attack power the original shows while dragging.
// The rebuild's ComputeChainAttack (turn.cpp:54-70, same decomp address) is the drag accumulator
// in const form: first non-phylactery skull pays base+attackBonus (halved vs a freezing mortal),
// every unbroken sword pays swordDamage (x5 magic, x2 Boost Damage). The drag loop
// (0x2c8dc:334-368 / ProcessInput @ 0x330c0:101-107) accumulates the identical terms.
int PreviewChainAttack(Game &g, const std::vector<Game::Cell> &path)
{
    PreviewScope scope(g, path);
    if (g.chain.empty()) return 0;
    return g.ComputeChainAttack();
}

// Tile::GetDamageType @ 0x644bc for the tile at (col,row) inside the preview chain; 0
// (DAMAGE_NONE) when the tile is not on the previewed path (UpdateCrossVisibility's chain loop
// only visits chain nodes).
int PreviewDamageType(Game &g, const std::vector<Game::Cell> &path, int col, int row)
{
    if (!InBoard(col, row)) return DAMAGE_NONE;
    PreviewScope scope(g, path);
    for (auto it = g.chain.begin(); it != g.chain.end(); ++it) {
        if ((*it)->col == col && (*it)->row == row)
            return ChainDamageType(g, **it, it);
    }
    return DAMAGE_NONE;
}

// The `cross visible` byte tile+0x144 of Game::UpdateCrossVisibility @ 0x2c5e8 — "this enemy
// dies if the chain lands":
//   in chain:  dt 2 (DAMAGE_NORMAL)  -> health <= max(0, +0xc23c - defence)        (:51-56)
//              dt 3 (DAMAGE_UPPERCUT) -> health <= max(0, +0xc17c + +0xc23c - defence) (:87-89 -> :55-56)
//              dt 0/1                 -> 0                                          (:120)
//   not in chain: only meat shields are re-evaluated (:123-139), same formula with +0xc23c;
//   every other tile keeps the 0 the empty-chain branch wrote (:30-43), which is the baseline a
//   fresh drag starts from — so outside the chain the preview returns false unless the tile is a
//   living meat shield the chain would kill.
bool PreviewWillDie(Game &g, const std::vector<Game::Cell> &path, int col, int row)
{
    if (!InBoard(col, row)) return false;
    const Tile &probe = g.At(col, row);
    PreviewScope scope(g, path);
    if (g.chain.empty()) return false;                       // UpdateCrossVisibility :30-43
    const int attack = g.ComputeChainAttack();               // +0xc23c
    for (auto it = g.chain.begin(); it != g.chain.end(); ++it) {
        if ((*it)->col != col || (*it)->row != row) continue;
        Tile &t = **it;
        int dt = ChainDamageType(g, t, it);
        int dmg;
        if (dt == DAMAGE_NORMAL) dmg = attack;                            // :52-55
        else if (dt == DAMAGE_UPPERCUT) dmg = g.swordDamage + attack;     // :88-89
        else return false;                                                // :120 (dt 0/1)
        return t.health <= std::max(0, dmg - t.defence);
    }
    // :123-139 — not a chain node: meat shield only.
    if (probe.type == TILE_SKULL && probe.enemyType == ENEMY_MEAT_SHIELD)
        return probe.health <= std::max(0, attack - probe.defence);
    return false;
}

// ---------------------------------------------------------------------------
// Class customisation.
// ---------------------------------------------------------------------------

// dr_class(klass, field) — pure reads of the CharacterClass record (character_class.h) plus the
// five per-class counters on Game. Field numbers are the M8 contract's.
int ClassGet(const Game &g, int klass, int field)
{
    if (klass < 0 || klass >= CLASS_COUNT) return -1;
    const CharacterClass &cc = *g.classes[static_cast<size_t>(klass)];
    switch (field) {
    case 0:  return cc.level;                                     // +0x54
    case 1:  return static_cast<int>(cc.race);                    // +0x8
    case 2:  return static_cast<int>(cc.inherentRace);            // +0xc
    case 3: case 4: case 5: case 6:
        return cc.perks[field - 3];                               // +0x10..+0x1c (255 = empty)
    case 7: case 8: case 9: case 10:
        return cc.flaws[field - 7];                               // +0x20..+0x2c
    case 11: {                                                    // IsPerkUnlocked @ 0x35d78
        int n = 0;
        for (int s = 0; s < 4; ++s) if (cc.IsPerkUnlocked(s)) ++n;
        return n;
    }
    case 12: return cc.GetReplacementSkillsAllowed();             // @ 0x35dc0
    case 13: return g.IsRaceUnlocked(cc.race) ? 1 : 0;            // Game::IsRaceUnlocked @ 0x248a0
    case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
    case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31: case 32: case 33:
        return cc.baseSkills[field - 14];                         // +0x30 (20 base skills)
    case 34: case 35: case 36: case 37: case 38: case 39: case 40: case 41: case 42: case 43:
    case 44: case 45: case 46: case 47: case 48: case 49: case 50: case 51: case 52: case 53:
        return cc.replacementSkills[field - 34];                  // +0x48 (0xff = none)
    // The 5 per-class counters (CharacterClass +0x58..+0x68), all written by Game::CheckEndGame
    // @ 0x2f960:61-63 (games played, enemies killed) and :79-91 (3 best scores, difficulty
    // 1/2/3 only — Easy has none). Rebuild fields: Game::classRuns/classKills/classBestScore
    // (combat.cpp:250-257), saved per class by SerialiseFullState @ 0x36494.
    //   CC +0x58 + i*0x184 == Game+0x14e04 (class base +0x14dac); label order per ClassScreen
    //   strings 0x5ba74..0x5bac0 (W4b): Games Played / Enemies Killed / Highest Normal / Hard /
    //   Harder Score. Easy has no best-score slot (difficulty 1..3 only).
    case 54: return g.classRuns[klass];                           // CC +0x58 "Games Played"
    case 55: return g.classKills[klass];                          // CC +0x5c "Enemies Killed"
    case 56: return g.classBestScore[klass][0];                   // CC +0x60 Highest Normal
    case 57: return g.classBestScore[klass][1];                   // CC +0x64 Highest Hard
    case 58: return g.classBestScore[klass][2];                   // CC +0x68 Highest Harder
    // A26q: CC +0x3c nonBaseSkills vector (Initialise @ 0x36524 tail; the ClassScreen tab-3
    // level row iterates 22 of them, UpdateAbilities @ 0x37670). 255 = index past the vector.
    case 59: case 60: case 61: case 62: case 63: case 64: case 65: case 66: case 67:
    case 68: case 69: case 70: case 71: case 72: case 73: case 74: case 75: case 76:
    case 77: case 78: case 79: case 80: {
        size_t i = static_cast<size_t>(field - 59);
        return i < cc.nonBaseSkills.size() ? cc.nonBaseSkills[i] : 255;
    }
    default: return -1;
    }
}

// dr_class_set(klass, what, slot, value) -> true = applied, false = refused.
// Refuses exactly what the original refuses; the accepted path calls the original's setter.
// Validate @ 0x36390 (dedupe + perk/flaw pairing + race reset) is NOT run here: the ClassScreen
// runs it on tab switch and on FadeOut @ 0x3730c, and running it per slot would clear a perk the
// player has not matched with its flaw yet. It runs at commit — Game::SelectClass -> Validate
// (character_class.cpp:250) — which is what ClassScreen::FadeOut does.
bool ClassSet(Game &g, int klass, int what, int slot, int value)
{
    if (klass < 0 || klass >= CLASS_COUNT) return false;
    CharacterClass &cc = *g.classes[static_cast<size_t>(klass)];
    switch (what) {
    case 0:   // ---- perk: CharacterClass::SetPerk @ 0x35e18 + the ClassScreen tab gates ----
        if (klass == CLASS_ADVENTURER) return false;    // str 0x5b56c "cannot be customised"
        if (slot < 1 || slot > 3) return false;         // slot 0 = inherent (SetPerk needs slot>0)
        if (value != PERK_NONE && (value < 0 || value >= PERK_COUNT)) return false;
        if (!cc.IsPerkUnlocked(slot)) return false;     // @ 0x35e18 level gate
        if (!g.IsPerkUnlocked(static_cast<PerkType>(value))) return false;   // Game::IsPerkUnlocked @ 0x24dec
        if (value != PERK_NONE && cc.HavePerk(static_cast<PerkType>(value))) return false;  // duplicate
        cc.SetPerk(slot, static_cast<PerkType>(value)); // @ 0x35e18
        return true;

    case 1:   // ---- flaw: CharacterClass::SetFlaw @ 0x35e6c ----
        if (klass == CLASS_ADVENTURER) return false;    // str 0x5b56c
        if (slot < 1 || slot > 3) return false;
        if (value != FLAW_NONE && (value < 0 || value >= FLAW_COUNT)) return false;
        if (!cc.IsPerkUnlocked(slot)) return false;     // flaw slots share the perk level gates
        if (value != FLAW_NONE && cc.HaveFlaw(static_cast<FlawType>(value))) return false;
        cc.SetFlaw(slot, static_cast<FlawType>(value)); // @ 0x35e6c
        return true;

    case 2:   // ---- race: Game::IsRaceUnlocked @ 0x248a0 + the SelectTab level-5 gate ----
        if (value < 0 || value >= RACE_COUNT) return false;
        if (klass == CLASS_ADVENTURER)                   // str 0x5b840 "can only be Human"
            return value == RACE_HUMAN;
        if (cc.level < 5)                                // str 0x5b970 "unlock at level 5"
            return value == static_cast<int>(cc.inherentRace);   // (Validate @0x36390 rule e)
        if (!g.IsRaceUnlocked(static_cast<RaceType>(value))) return false;
        cc.race = static_cast<RaceType>(value);
        return true;

    case 3: { // ---- replacement skill: CharacterClass::SetReplacementSkill @ 0x364dc ----
        if (slot < 1 || slot >= 20) return false;        // slot 0 = the class skill (str 0x5c6d4)
        if (value == 0xff) {                             // clear
            if (cc.replacementSkills[slot] == 0xff) return true;   // already empty
            cc.SetReplacementSkill(static_cast<unsigned>(slot), 0xff);
            return true;
        }
        if (value < 0 || value >= 0x2a) return false;    // 42 skills (Initialise @ 0x36524:658-677)
        // The ClassScreen only drags from the non-base list (GetNonBaseSkillIndex @ 0x362d4
        // searches +0x3c), so a base skill can never reach this setter.
        if (cc.IsBaseSkill(value)) return false;
        if (cc.replacementSkills[slot] == 0xff &&        // filling an empty slot costs budget,
            cc.GetReplacementSkillCount() >= cc.GetReplacementSkillsAllowed())  // @ 0x35dc0
            return false;
        cc.SetReplacementSkill(static_cast<unsigned>(slot), value);   // @ 0x364dc
        return true;
    }
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Session helpers.
// ---------------------------------------------------------------------------

// dr_new_game(seed, difficulty, mode, klass). Order mirrors the original flow (and drcli):
// commit the class first (ClassScreen::FadeOut @ 0x3730c -> Game::SelectClass, which falls back
// to the Adventurer for a locked pick), then start — NewGame @ 0x2de68 clears the challenge flag,
// StartChallenge @ 0x2de10 sets it, so the challenge modes call InitNewGame @ 0x2d7a4 directly
// (tests/drcli.cpp:135-142 does the same). Game+0x14330 (mode) is written here because neither
// NewGame nor StartChallenge touches it: the original sets it from the ModeScreen/save
// (ModeScreen::UpdateMode @ 0x55dec), the Game ctor default is MODE_CLASSIC.
void StartGame(Game &g, uint32_t seed, int difficulty, int mode, int klass)
{
    g.seedOverride = seed;
    if (klass >= 0 && klass < CLASS_COUNT) g.SelectClass(klass);
    const Difficulty d = static_cast<Difficulty>(ClampI(difficulty, 0, DIFF_COUNT - 1));
    if (mode == MODE_SPRINT || mode == MODE_PRETZEL) {
        g.difficulty = d;
        g.challenge = true;                    // Game+0x14335 (StartChallenge @ 0x2de10:10)
        g.mode = static_cast<GameMode>(mode);  // Game+0x14330
        g.InitNewGame();                       // @ 0x2d7a4
    } else {
        g.mode = MODE_CLASSIC;                 // Game+0x14330 = 3 (GameMode default)
        g.NewGame(d);                          // @ 0x2de68 (clears +0x14335)
    }
}

// dr_skill_activate(slot) -> 1 when the original's release path would run: the button tap only
// activates if Skill::CanActivate @ 0x60204 passes (Interface::ProcessInput @ 0x49738:86-88);
// Game::ActivateSkill (skills.cpp:310) re-checks it and then does Activate @ 0x60210 -> charge
// reset -> Dragonkin FireBreath @ 0x29cfc -> Interface::UpdateSkills.
bool TryActivateSkill(Game &g, int slot)
{
    if (slot < 0 || slot >= static_cast<int>(g.skills.size())) return false;
    if (!g.skills[static_cast<size_t>(slot)]->CanActivate()) return false;
    g.ActivateSkill(slot);
    return true;
}

// dr_unlock_next(field) over Game::unlockPopups (UnlockMenu::Show @ 0x671ec, recorded by
// Game::ShowUnlockMenu, turn.cpp:431-434). Contract: call field 0 first, then 1 — field 0 pops
// the oldest popup and returns its ClassType, field 1 answers the level of that same popup
// (the latch is API session bookkeeping, not game state: unlockPopups itself is the game state).
int PopUnlock(Game &g, int field)
{
    static int s_level = -1;    // level of the popup field 0 last popped
    if (field == 0) {
        if (g.unlockPopups.empty()) { s_level = -1; return -1; }
        s_level = g.unlockPopups.front().level;
        const int cls = g.unlockPopups.front().classType;
        g.unlockPopups.erase(g.unlockPopups.begin());
        return cls;
    }
    if (field == 1) return s_level;
    return -1;
}

// ---------------------------------------------------------------------------
// Player name — NameScreen (task W6; analysis/features/NAME_SCORES_W6.md).
// ---------------------------------------------------------------------------
// Game+0xc1f8 holds the name (17 bytes: strncpy 0x10 + forced NUL at +0xc208, max 16 chars).
// It persists across games (NewGame @ 0x2de68 passes it into NameScreen::Show; InitNewGame never
// clears it) and is part of the run save (SerialiseState @ 0x265f4:114).

// Play-button copy, ProcessInput @ 0x57a10:498-501 (also JNI SetName @ 0x23de0:23):
// strncpy(Game+0xc1f8, buf, 0x10) + NUL at +0xc208 -> truncate to 16, never refuse.
void SetPlayerName(Game &g, const char *s)
{
    if (!s) s = "";
    size_t n = 0;
    while (n < 0x10 && s[n] != '\0') ++n;
    g.playerName.assign(s, n);
}

// NameScreen::GenerateRandomName @ 0x5615c — name tables at ELF 0x5da04 (50 x 5 B) and
// 0x5db00 (49 x 5 B, index 0 = empty), formats "%s%s" / "%s%s %s%s" at ELF 0x5dbf8 / 0x5dc00.
// Draws RandI on stream 0 (cosmetic — RNG.md). The original writes the NameScreen buffer; the
// Play button later copies it to Game+0xc1f8 (:498-501), which is what this helper does in one
// step. RNG safety: the original generates between TWO InitNewGame calls (NewGame @ 0x2de68
// seeds all streams, then Play @ :504 calls InitNewGame again which re-seeds everything), so the
// draws can never shift the started game's RNG — likewise here when called before StartGame.
std::string GenerateRandomName(Game &g)
{
    static const char kT1[50][5] = {
        "Ar","Ang","Bal","Bee","Clen","Coy","Def","Dil","En","Eck",
        "Fan","Fen","Gam","Gil","Hel","Har","Il","Ink","Jos","Jen",
        "Kart","Kea","Lid","Lin","Mer","Mond","Neg","Non","Orn","Oart",
        "Pey","Put","Quo","Quin","Rin","Roz","So","Sit","Tec","Tint",
        "Vin","Vam","Wel","Wit","Xer","Xin","Ye","Yon","Zil","Zar"};
    static const char kT2[49][5] = {
        "","and","aar","bole","bea","cor","cut","din","dier","een",
        "ell","fet","fall","gren","gap","het","hob","inn","ire","jat",
        "jel","kenn","kaf","lyt","lack","mon","mud","ney","nant","ons",
        "ore","pol","pent","rew","rog","sear","sen","ten","treb","un",
        "ure","ved","vie","wim","wed","yet","yore","zim","zes"};
    char buf[32];
    if (RandI(0, 1, RAND_COSMETIC) == 0) {
        std::snprintf(buf, sizeof buf, "%s%s", kT1[RandI(0, 0x31, RAND_COSMETIC)],
                      kT2[RandI(0, 0x30, RAND_COSMETIC)]);
    } else {
        std::snprintf(buf, sizeof buf, "%s%s %s%s", kT1[RandI(0, 0x31, RAND_COSMETIC)],
                      kT2[RandI(0, 0x30, RAND_COSMETIC)], kT1[RandI(0, 0x31, RAND_COSMETIC)],
                      kT2[RandI(0, 0x30, RAND_COSMETIC)]);
    }
    SetPlayerName(g, buf);
    return g.playerName;
}

// ---------------------------------------------------------------------------
// High scores — ScoreScreen::AddScore @ 0x5cc5c.
// ---------------------------------------------------------------------------
// Entry layout (0x38 B) exactly as AddScore writes it:
//   +0x00 name[20]  (Game+0xc1f8 playerName — strcpy at :96; shown by InitialisePage @0x5d0e0:146;
//                    exposed as dr_score_name, W6)
//   +0x14 turn      (param_2)   +0x18/+0x1c/+0x20 mode columns (param_3..5)
//   +0x24 score     (param_6, the sort key)   +0x28..+0x34 4 skill ids (SetSkills @ 0x5c398)
// AddScore takes no class argument — ScoreEntry::cls is a REBUILD-ADDED field (screens only)
// taken from Game+0x15cd4 at insert time.
//
// Page = ScoreType: CheckEndGame @ 0x2f960 passes Game+0x14328 (difficulty) for a classic run and
// Game+0x14330 (GameMode 0/1) for a challenge run into the typed overloads — and the GameMode
// overload AddScore @ 0x5ce88 adds 4 (Thumb `adds r4,#4`) so sprint/pretzel land on ScoreType
// SCORE_SPRINT/SCORE_PRETZEL = 4/5 (the Difficulty overload @ 0x5cea8 passes through unchanged).
int ScoreBook::Add(const Game &g, int page)
{
    if (page < 0 || page >= PAGES) return -1;
    ScoreEntry *row = e[page];
    int count = 0;
    while (count < ROWS && row[count].score >= 0) ++count;

    const int ns = g.GetScore();                       // GetScore @ 0x2f868
    int rank = 0;
    // AddScore @ 0x5cc5c:29-54 — insert before the first existing entry with a smaller score.
    if (!(count < 1 || row[0].score < ns)) {
        int i = 1;
        for (;;) {
            if (count <= i) { rank = i; break; }
            if (!(ns <= row[i].score)) { rank = i; break; }
            ++i;
        }
        if (rank == 10) rank = 9;                       // :50-53
    }
    // :57-95 shift everything below rank one slot down (top slot clipped at index 9).
    const int top = count > 8 ? 9 : count;
    if (rank < top)
        for (int i = top; i > rank; --i) row[i] = row[i - 1];

    ScoreEntry &t = row[rank];
    t = ScoreEntry{};
    std::strncpy(t.name, g.playerName.c_str(), 16);   // AddScore @0x5cc5c:96 strcpy(Game+0xc1f8)
    t.name[16] = '\0';
    t.cls  = g.activeClass;                             // REBUILD-ADDED (see header)
    t.turn = g.turn;                                    // +0xc
    const int xp  = g.xp + g.xpHundreds;                // +0xc170 + +0xc210
    const int gp  = g.gold + g.goldHundreds;            // +0xc178 + +0xc214
    const int up  = g.upgradePoints + g.upHundreds;     // +0xc174 + +0xc218
    if (!g.challenge) {                                 // CheckEndGame @ 0x2f960:64-75
        t.col[0] = xp; t.col[1] = gp; t.col[2] = up;
    } else if (g.mode == MODE_PRETZEL) {                // :94-103
        t.col[0] = g.chainAvgX10;                       // +0x15e64 Avg
        t.col[1] = g.longestChain;                      // +0x15e68 Max
        t.col[2] = g.chainsMade;                        // +0x15e60 Chains
    } else {                                            // MODE_SPRINT, :104-125
        t.col[0] = xp / 4; t.col[1] = gp / 4; t.col[2] = up / 4;
    }
    t.score = ns;
    for (int i = 0; i < 4; ++i)                         // SetSkills @ 0x5c398 (CheckEndGame:152)
        t.skills[i] = i < static_cast<int>(g.skills.size()) ? g.skills[static_cast<size_t>(i)]->type : -1;
    return rank;
}

const ScoreEntry *ScoreBook::Get(int page, int rank) const
{
    if (page < 0 || page >= PAGES || rank < 0 || rank >= ROWS) return nullptr;
    if (e[page][rank].score < 0) return nullptr;
    return &e[page][rank];
}

// dr_meta_save payload for the scores: magic 'DRSC', version, 6x10 fixed entries.
// v2 (W6): each entry = name[20] + 10 ints; v1 blobs (no name) still load.
std::vector<uint8_t> ScoreBook::Serialise() const
{
    std::vector<uint8_t> b;
    auto put = [&b](int v) {                              // little-endian int32
        b.push_back(static_cast<uint8_t>(v & 0xff));
        b.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
        b.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
        b.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
    };
    b.push_back('D'); b.push_back('R'); b.push_back('S'); b.push_back('C'); b.push_back(2);
    for (int p = 0; p < PAGES; ++p)
        for (int r = 0; r < ROWS; ++r) {
            const ScoreEntry &t = e[p][r];
            for (int i = 0; i < 20; ++i)
                b.push_back(static_cast<uint8_t>(t.name[i]));  // entry+0x00, NUL-padded
            put(t.cls); put(t.turn);
            put(t.col[0]); put(t.col[1]); put(t.col[2]); put(t.score);
            put(t.skills[0]); put(t.skills[1]); put(t.skills[2]); put(t.skills[3]);
        }
    return b;
}

bool ScoreBook::Deserialise(const uint8_t *bytes, int len)
{
    if (!bytes || len < 5) return false;
    if (bytes[0] != 'D' || bytes[1] != 'R' || bytes[2] != 'S' || bytes[3] != 'C') return false;
    const int version = bytes[4];
    if (version != 1 && version != 2) return false;
    const int stride = version == 2 ? 60 : 40;
    const int need = 5 + PAGES * ROWS * stride;
    if (len < need) return false;
    auto get = [&bytes](int off) {
        return static_cast<int>(static_cast<uint32_t>(bytes[off]) |
                                (static_cast<uint32_t>(bytes[off + 1]) << 8) |
                                (static_cast<uint32_t>(bytes[off + 2]) << 16) |
                                (static_cast<uint32_t>(bytes[off + 3]) << 24));
    };
    int off = 5;
    for (int p = 0; p < PAGES; ++p)
        for (int r = 0; r < ROWS; ++r) {
            ScoreEntry & t = e[p][r];
            t = ScoreEntry{};
            if (version == 2) {
                for (int i = 0; i < 20 && bytes[off + i] != 0; ++i)
                    t.name[i] = static_cast<char>(bytes[off + i]);
                off += 20;
            }
            t.cls = get(off); t.turn = get(off + 4);
            t.col[0] = get(off + 8); t.col[1] = get(off + 12); t.col[2] = get(off + 16);
            t.score = get(off + 20);
            t.skills[0] = get(off + 24); t.skills[1] = get(off + 28);
            t.skills[2] = get(off + 32); t.skills[3] = get(off + 36);
            off += 40;
        }
    return true;
}

// ---------------------------------------------------------------------------
// Wall clock (task W7).
// ---------------------------------------------------------------------------

// Game::SetClockState @ 0x25038: +0x142fc = (byte)+0x142f9 ^ on, then +0x142f9 = on. The
// original then calls Interface::SetClockState @ 0x487dc (clock-quad +0xcac UV swap, flag
// +0x11d4, GameMenu::SetClockState @ 0x44428 quad-12 UV swap) — pure presentation, done by
// the page. The clock STRING is not native: DungeonRaidRenderer.onDrawFrame formats the
// current time of day with SimpleDateFormat("h:mm a") and calls SetClockString every frame
// while IsClockEnabled — a wall clock, not a run timer.
void SetClockState(Game &g, bool on)
{
    g.clockStateChanged = g.clockEnabled != on;   // +0x142fc = old ^ new
    g.clockEnabled = on;                          // +0x142f9
}

// Game::IsClockEnabled: JNI wrapper Game_1IsClockEnabled @ 0x234dc returns *(Game + 0x142f9).
bool ClockEnabled(const Game &g) { return g.clockEnabled; }

} // namespace dr
