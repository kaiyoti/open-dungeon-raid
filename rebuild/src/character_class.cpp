// Character-class system: CharacterClass::Initialise and the class-level rules (task C1).
// Every function mirrors its original decomp file (addresses = Ghidra base, ELF VMA + 0x10000).
// Game-level pieces of the class system live here too: SelectClass (ClassScreen commit path),
// IsRaceUnlocked, IsPerkUnlocked, CheckIfAllClassesMaxed. Persistence (SerialiseClasses /
// DeserialiseClasses / SerialiseFullState) is in save.cpp.
#include "dr/game.h"
#include "dr/character_class.h"

namespace dr {

namespace {

int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Base-skill lists per class (CharacterClass::Initialise @ 0x36524: 20 push_backs per case;
// verified byte-identical by analysis/features/CLASSES.md §1 against the old game.cpp table that
// had been dumped live from the original via tools/emu). List order matters: index 0 is "the
// class skill" (GetSkillPool @ 0x3707c, menus.cpp).
const int kBaseSkills[CLASS_COUNT][20] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19},
    {31, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13, 14, 16, 18, 21, 22, 23, 24, 25, 29},
    {32, 2, 3, 4, 6, 8, 9, 11, 12, 15, 16, 17, 18, 19, 20, 22, 23, 24, 25, 27},
    {33, 1, 2, 5, 6, 7, 8, 9, 10, 12, 13, 15, 18, 19, 21, 20, 25, 26, 28, 29},
    {34, 3, 4, 5, 7, 9, 10, 11, 12, 13, 14, 16, 18, 19, 21, 20, 22, 23, 25, 27},
    {35, 1, 2, 3, 6, 7, 8, 9, 10, 12, 15, 17, 18, 21, 20, 24, 25, 26, 28, 29},
    {36, 0, 2, 3, 4, 6, 8, 9, 10, 11, 12, 13, 14, 15, 17, 18, 22, 23, 24, 25},
    {37, 2, 3, 4, 5, 8, 9, 11, 12, 13, 14, 16, 18, 19, 21, 20, 23, 24, 25, 27},
    {38, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 19, 21, 22, 23, 25, 27},
    {39, 0, 1, 4, 5, 7, 8, 9, 12, 14, 15, 16, 18, 21, 22, 23, 24, 25, 27, 28}};

// Per-class inherent race / perk slot 0 / flaw slot 0 (Initialise @ 0x36524, per-case writes to
// this+8/this+0xc, this+0x10, this+0x20; CLASSES.md §1 table).
// A26q: the race list was an identity placeholder — the original's per-case values are
// this+8 = [0,1,6,4,5,9,3,8,2,7] (Initialise @0x36524:213,236,283,331,377,424,471,518,565,612;
// emu capture class_t0_c5 header race icon = (459,121) = race 9 for class 5 confirms).
const RaceType kRace[CLASS_COUNT] = {
    RACE_HUMAN, RACE_ELF, RACE_HALFLING, RACE_FAUN, RACE_ORC,
    RACE_CHANGELING, RACE_SAUREN, RACE_DRAGONKIN, RACE_DWARF, RACE_WEREWOLF};
const int kPerk0[CLASS_COUNT] = {
    PERK_NONE, PERK_AGILE, PERK_DARING, PERK_FAITHFUL, PERK_STRONG,
    PERK_GIFTED, PERK_FRUGAL, PERK_INTIMIDATING, PERK_TOUGH, PERK_CUNNING};
const int kFlaw0[CLASS_COUNT] = {
    FLAW_NONE, FLAW_SLIM, FLAW_COCKY, FLAW_CAUTIOUS, FLAW_RECKLESS,
    FLAW_FRAIL, FLAW_CLUMSY, FLAW_ZEALOUS, FLAW_NOISY, FLAW_SNEAKY};

} // namespace

// CharacterClass::Initialise(ClassType, Game*) @ 0x36524 — called for i=0..9 from Game::Start @
// 0x260ac:168-172 (rebuild: Game ctor). Sets Game* (+0), klass (+4), level (+0x54) = 0 (1 for
// class 0), all perk/flaw slots = 0xff, then race (+8) = inherent race (+0xc) = the class's race,
// perk slot 0 / flaw slot 0, and the 20 base skills; finally every id 0..0x29 not in the base
// list goes to the non-base vector (+0x3c) and the replacement vector (+0x48) is filled with one
// 0xff per base skill. (No Validate call — contrary to an earlier task note; Validate first runs
// when the player commits the class, ClassScreen::FadeOut @ 0x3730c.)
void CharacterClass::Initialise(int classIdx, Game *g)
{
    game = g;
    klass = static_cast<ClassType>(classIdx);
    level = 0;
    for (int i = 0; i < 4; ++i) { perks[i] = PERK_NONE; flaws[i] = FLAW_NONE; }
    race = inherentRace = kRace[classIdx];
    if (classIdx == CLASS_ADVENTURER) {
        level = 1;                       // only class 0 starts unlocked (Initialise @0x36524:210-211)
        perks[0] = kPerk0[0];            // PERK_NONE (0xff), kept for symmetry with the other cases
        flaws[0] = kFlaw0[0];
    } else {
        perks[0] = kPerk0[classIdx];
        flaws[0] = kFlaw0[classIdx];
    }
    for (int j = 0; j < 20; ++j) baseSkills[j] = kBaseSkills[classIdx][j];
    nonBaseSkills.clear();
    for (int id = 0; id < 0x2a; ++id)    // 42 skills (Initialise @0x36524:658-677)
        if (!IsBaseSkill(id)) nonBaseSkills.push_back(id);
    for (int j = 0; j < 20; ++j) replacementSkills[j] = 0xff;   // @0x36524:678-702
}

// CharacterClass::IsPerkUnlocked(int) @ 0x35d78 — unlocks a perk/flaw SLOT by class level:
// slot 0 at level != 0, slot 1 at level > 2, slot 2 at level > 6, slot 3 at level >= 9.
bool CharacterClass::IsPerkUnlocked(int slot) const
{
    switch (slot) {
    case 0: return level != 0;
    case 1: return level > 2;
    case 2: return level > 6;
    case 3: return level >= 9;
    default: return false;
    }
}

// CharacterClass::GetReplacementSkillsAllowed @ 0x35dc0 — how many base skills may be replaced
// at the current level: lv<2:0, lv 2-3:2, 4-5:4, 6-7:6, 8-9:8, lv10:10.
int CharacterClass::GetReplacementSkillsAllowed() const
{
    if (level < 2) return 0;
    if (level < 4) return 2;
    if (level < 6) return 4;
    if (level < 8) return 6;
    return level == 10 ? 10 : 8;
}

// CharacterClass::GetPerkCount @ 0x35f4c — perk slots != 0xff; 0 for the Adventurer (klass == 0
// short-circuits the whole count, @0x35f4c:12).
int CharacterClass::GetPerkCount() const
{
    if (klass == CLASS_ADVENTURER) return 0;
    int n = 0;
    for (int p : perks) if (p != PERK_NONE) ++n;
    return n;
}

// CharacterClass::GetFlawCount @ 0x35f74 — same for flaw slots.
int CharacterClass::GetFlawCount() const
{
    if (klass == CLASS_ADVENTURER) return 0;
    int n = 0;
    for (int f : flaws) if (f != FLAW_NONE) ++n;
    return n;
}

// CharacterClass::SetLevel @ 0x35f9c — ClampI(l, 0, 10) into the +0x54 byte.
void CharacterClass::SetLevel(int l) { level = ClampI(l, 0, 10); }

// CharacterClass::LevelUp @ 0x3600c — ClampI(level+1, 1, 10) then UnlockAchievement.
void CharacterClass::LevelUp()
{
    level = ClampI(level + 1, 1, 10);
    UnlockAchievement();
}

// CharacterClass::UnlockAchievement @ 0x35fb0 — for a real class (klass != 0): queue the class
// achievement (ids 1..9), at level > 5 also the race one (race(+0xc)+9 = 10..18), at level 10 the
// master one (klass+18 = 19..27), then Game::CheckIfAllClassesMaxed @ 0x2835c. All of it goes
// through the Platform UI hook (no game state).
void CharacterClass::UnlockAchievement()
{
    if (klass == CLASS_ADVENTURER) return;
    if (level != 0) {
        game->fx().QueueAchievement(static_cast<Achievement>(klass), 100.0f);
        if (level > 5) {
            game->fx().QueueAchievement(static_cast<Achievement>(inherentRace + 9), 100.0f);
            if (level == 10)
                game->fx().QueueAchievement(static_cast<Achievement>(klass + 18), 100.0f);
        }
    }
    game->CheckIfAllClassesMaxed();
}

// CharacterClass::SetPerk(int, PerkType) @ 0x35e18 — only slots the class level has unlocked,
// never slot 0 (the inherent perk), and never a duplicate.
void CharacterClass::SetPerk(int slot, PerkType p)
{
    if (slot > 0 && IsPerkUnlocked(slot) && !HavePerk(p)) perks[slot] = p;
}

// CharacterClass::SetFlaw(int, FlawType) @ 0x35e6c — same gates (calls IsPerkUnlocked, the flaw
// slots share the perk-slot level gates).
void CharacterClass::SetFlaw(int slot, FlawType f)
{
    if (slot > 0 && IsPerkUnlocked(slot) && !HaveFlaw(f)) flaws[slot] = f;
}

// CharacterClass::SetReplacementSkill(uint, SkillType) @ 0x364dc — index must be > 0 (the class
// skill in slot 0 is not replaceable) and < baseSkill count; filling a still-empty slot costs one
// of the GetReplacementSkillsAllowed budget, overwriting an existing replacement is free.
void CharacterClass::SetReplacementSkill(unsigned slot, int skill)
{
    if (slot > 0 && slot < 20u) {
        if (replacementSkills[slot] == 0xff) {
            if (GetReplacementSkillCount() < GetReplacementSkillsAllowed())
                replacementSkills[slot] = skill;
        } else {
            replacementSkills[slot] = skill;
        }
    }
}

// CharacterClass::GetNonBaseSkillIndex(SkillType) @ 0x362d4 — index in the non-base vector, -1 if
// the skill is a base skill or unknown.
int CharacterClass::GetNonBaseSkillIndex(int id) const
{
    for (size_t i = 0; i < nonBaseSkills.size(); ++i)
        if (nonBaseSkills[i] == id) return static_cast<int>(i);
    return -1;
}

// CharacterClass::IsReplacementSkill @ 0x36304 / IsBaseSkill @ 0x36334 — membership tests.
bool CharacterClass::IsReplacementSkill(int id) const
{
    for (int x : replacementSkills) if (x == id) return true;
    return false;
}

bool CharacterClass::IsBaseSkill(int id) const
{
    for (int x : baseSkills) if (x == id) return true;
    return false;
}

// CharacterClass::GetReplacementSkillCount @ 0x36364 — replacement slots != 0xff.
int CharacterClass::GetReplacementSkillCount() const
{
    int n = 0;
    for (int x : replacementSkills) if (x != 0xff) ++n;
    return n;
}

// CharacterClass::Validate @ 0x36390 — (a) drop duplicate perks (first occurrence wins, later
// equal slot -> 0xff); (b) same for flaws; (c) a perk with no flaw IN THE SAME SLOT is cleared
// (pairing is by slot, not by perk/flaw id); (d) walk the replacement vector with a counter
// pre-loaded from GetReplacementSkillCount() and clear slots once it goes negative — as written
// the counter equals the number of non-0xff slots, so it never reaches -1 and nothing is cleared
// (the decompiler clearly calls GetReplacementSkillCount, not GetReplacementSkillsAllowed —
// ported as-is); (e) klass != 0 && level < 5 -> race(+8) = inherent race(+0xc) (you cannot have
// changed race before it unlocks at level 5).
void CharacterClass::Validate()
{
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (i != j && perks[i] == perks[j]) perks[j] = PERK_NONE;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (i != j && flaws[i] == flaws[j]) flaws[j] = FLAW_NONE;
    for (int i = 0; i < 4; ++i)
        if (perks[i] != PERK_NONE && flaws[i] == FLAW_NONE) perks[i] = PERK_NONE;
    int left = GetReplacementSkillCount();
    for (int i = 0; i < 20; ++i) {
        if (replacementSkills[i] != 0xff) --left;
        if (left < 0) replacementSkills[i] = 0xff;
    }
    if (klass != CLASS_ADVENTURER && level < 5) race = inherentRace;
}

// ---------------------------------------------------------------------------
// Game-side class system.
// ---------------------------------------------------------------------------

// Game::CheckIfAllClassesMaxed @ 0x2835c — classes 1..9 all at level >= 10 -> ACH_CLASSY (0x1c).
void Game::CheckIfAllClassesMaxed()
{
    for (int i = 1; i < CLASS_COUNT; ++i)
        if (classes[i]->level < 10) return;
    fx().QueueAchievement(ACH_CLASSY, 100.0f);
}

// Game::SelectClass(int) — the headless form of the class-commit path: ClassScreen::Process tap
// @ 0x3a598:506-518 (and ClassScreen::FadeOut @ 0x3730c, which adds Validate + SerialiseClasses).
// Validate the chosen class; if its level byte is 0 (locked) the selection falls back to class 0
// (Adventurer); store it in activeClass (+0x15cd4); set the +0x15cd8 flag (=1, UNKNOWN meaning —
// no reader in analysis/decomp); FadeOut then also serialises all 10 classes.
void Game::SelectClass(int idx)
{
    if (idx < 0 || idx >= CLASS_COUNT) return;   // harness guard: the UI only ever passes 0..9
    classes[idx]->Validate();                    // ClassScreen::FadeOut @ 0x3730c
    if (classes[idx]->level == 0) idx = 0;       // locked -> Adventurer (FadeOut / Process tap)
    activeClass = idx;                           // Game+0x15cd4
    unknown15cd8 = 1;                            // Game+0x15cd8 (UNKNOWN; set here and @ Start:181)
    SerialiseClasses();                          // FadeOut @ 0x3730c -> Game::SerialiseClasses @ 0x24d98
}

// Game::SetClassLevel(ClassType, int) @ 0x24d28 — raise-only: SetLevel(arg) only when the class's
// current level byte is lower (SetLevel itself clamps 0..10, @ 0x35f9c). Only caller in the 1.2.11
// binary is the dead OpenFeint path (CLASSES.md §3); the headless harnesses use it to set up a
// class run (tools/emu/game.py select_class, drcli --class/--class-level).
void Game::SetClassLevel(ClassType c, int l)
{
    if (c < 0 || c >= CLASS_COUNT) return;   // harness guard: original indexes by ClassType
    if (classes[c]->level < l) classes[c]->SetLevel(l);
}

// Game::IsRaceUnlocked(RaceType) @ 0x248a0 — a race is unlocked when the class whose INHERENT
// race (+0xc) it is has level > 4 (level 5 unlocks race change); the Adventurer's race (Human)
// is always unlocked (klass == 0 short-circuit); no owning class -> false.
bool Game::IsRaceUnlocked(RaceType r) const
{
    for (int i = 0; i < CLASS_COUNT; ++i) {
        if (r == classes[i]->inherentRace) {
            if (classes[i]->klass != CLASS_ADVENTURER) return classes[i]->level > 4;
            return true;
        }
    }
    return false;
}

// Game::IsPerkUnlocked(PerkType) @ 0x24dec — perk 0 (Unexpected) is always unlocked; any other
// perk is owned by exactly one class as its slot-0 perk, and unlocks when that class is unlocked
// at all (CharacterClass::IsPerkUnlocked(slot 0) = level != 0).
bool Game::IsPerkUnlocked(PerkType p) const
{
    if (p == PERK_UNEXPECTED) return true;
    for (int i = 0; i < CLASS_COUNT; ++i)
        if (p == classes[i]->perks[0]) return classes[i]->IsPerkUnlocked(0);
    return false;
}

} // namespace dr
