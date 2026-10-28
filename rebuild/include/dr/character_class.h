// Character class slot (10 of them in Game at +0x14dac, stride 0x184); analysis/progression/CLASS_SKETCH.md,
// PERKS_FLAWS.md, analysis/features/CLASSES.md.
// Original layout: +0 Game*, +4 klass, +8 race, +0xc inherent race, +0x10 perks[4], +0x20 flaws[4],
// +0x30 baseSkills vector, +0x3c nonBaseSkills vector, +0x48 replacementSkills vector, +0x54 level byte,
// +0x58..+0x68 five per-class counters (modelled on Game as classRuns/classKills/classBestScore —
// combat.cpp extras), +0x16c skill-pool set cache (rebuild computes the pool on demand in menus.cpp).
#pragma once
#include <vector>
#include "enums.h"

namespace dr {

class Game;            // +0 back-pointer; Initialise stores it (used by UnlockAchievement)

struct CharacterClass {
    Game *game = nullptr;                // +0 (CharacterClass::Initialise @ 0x36524)
    ClassType klass = CLASS_ADVENTURER;  // +4
    RaceType  race = RACE_HUMAN;         // +0x8 active race (serialised); chosen in the UI once
    RaceType  inherentRace = RACE_HUMAN; // +0xc the class's fixed race; Initialise sets +8 and +0xc
                                         //   together; Validate @ 0x36390 copies +0xc -> +8 when
                                         //   klass != 0 && level < 5 (race change unlocks at level 5)
    int perks[4] = {PERK_NONE, PERK_NONE, PERK_NONE, PERK_NONE};   // +0x10..+0x1c (slot 0 = inherent)
    int flaws[4] = {FLAW_NONE, FLAW_NONE, FLAW_NONE, FLAW_NONE};   // +0x20..+0x2c
    int level = 1;                         // +0x54 (byte; 0 = locked, 1..10)
    // +0x30 vector<int>[20]: the class's base skills (CharacterClass::Initialise @ 0x36524;
    // dumped from the running original: class 0 = skills 0..19, classes 1..9 start with their
    // class-unique skill 0x1f+class). Used by GetSkillPool @ 0x3707c and IsSkillUnlocked @ 0x248e8.
    int baseSkills[20] = {0};
    // +0x3c vector<int>: every skill id 0..0x29 not in baseSkills (Initialise @ 0x36524 tail;
    // GetNonBaseSkillIndex @ 0x362d4 searches it).
    std::vector<int> nonBaseSkills;
    // +0x48 vector<int>[20]: per-slot replacement for the class's base skill (+0x30), 0xff = keep base
    // (CharacterClass::GetSkillPool @ 0x3707c, Validate @ 0x36390). Entries 1..19 are saved (low byte).
    int replacementSkills[20] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    bool HavePerk(PerkType p) const { if (p == PERK_NONE) return false; for (int x : perks) if (x == p) return true; return false; }
    bool HaveFlaw(FlawType f) const { if (f == FLAW_NONE) return false; for (int x : flaws) if (x == f) return true; return false; }
    EnemyType MortalEnemy() const;         // CharacterClass::GetMortalEnemyType (R task)

    // ---- class system (src/character_class.cpp, task C1) ------------------------------------
    void Initialise(int classIdx, Game *g);   // Initialise(ClassType, Game*) @ 0x36524
    bool IsPerkUnlocked(int slot) const;      // @ 0x35d78 slot gate: 0:level!=0 1:>2 2:>6 3:>=9
    int  GetReplacementSkillsAllowed() const; // @ 0x35dc0
    int  GetPerkCount() const;                // @ 0x35f4c (0 unless klass != CLASS_ADVENTURER)
    int  GetFlawCount() const;                // @ 0x35f74
    void SetLevel(int l);                     // @ 0x35f9c ClampI(l, 0, 10)
    void LevelUp();                           // @ 0x3600c ClampI(level+1, 1, 10) + UnlockAchievement
    void UnlockAchievement();                 // @ 0x35fb0 queues class/race/master achievements
    void SetPerk(int slot, PerkType p);       // @ 0x35e18 (slot > 0 only; slot 0 = inherent)
    void SetFlaw(int slot, FlawType f);       // @ 0x35e6c
    void SetReplacementSkill(unsigned slot, int skill);  // @ 0x364dc
    int  GetNonBaseSkillIndex(int id) const;  // @ 0x362d4 (-1 = absent)
    bool IsReplacementSkill(int id) const;    // @ 0x36304
    bool IsBaseSkill(int id) const;           // @ 0x36334
    int  GetReplacementSkillCount() const;    // @ 0x36364 (entries != 0xff)
    void Validate();                          // @ 0x36390 dedupe + perk/flaw pairing + race reset
};

} // namespace dr
