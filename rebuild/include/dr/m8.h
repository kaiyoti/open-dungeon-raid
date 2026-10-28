// M8 web-API game-side helpers (task W3). Every function here mirrors a decompiled original
// (addresses = Ghidra base, ELF VMA + 0x10000) and lives in rebuild/src/m8.cpp; web/api.cpp only
// calls these and packs the results. Kept out of game.h so the per-file extras blocks stay with
// their owners.
#pragma once
#include <cstdint>
#include <vector>
#include "game.h"

namespace dr {

// ---- drag preview -----------------------------------------------------------------------------
// Side-effect free forms of Game::UpdateCrossVisibility @ 0x2c5e8 / the preview half of
// Game::ProcessTileSelection @ 0x2c8dc. None of the three writes Game or Tile state and none
// consumes an RNG stream (the original's stream-0 label draws are presentation and are skipped;
// see m8.cpp).
int  PreviewChainAttack(Game &g, const std::vector<Game::Cell> &path);   // +0xc23c
int  PreviewDamageType(Game &g, const std::vector<Game::Cell> &path, int col, int row);
bool PreviewWillDie(Game &g, const std::vector<Game::Cell> &path, int col, int row);

// ---- class customisation (ClassScreen) ---------------------------------------------------------
int  ClassGet(const Game &g, int klass, int field);
bool ClassSet(Game &g, int klass, int what, int slot, int value);

// ---- session helpers --------------------------------------------------------------------------
// dr_new_game: seed + class commit (ClassScreen::FadeOut @ 0x3730c path) + NewGame/StartChallenge.
void StartGame(Game &g, uint32_t seed, int difficulty, int mode, int klass);
bool TryActivateSkill(Game &g, int slot);   // Interface::ProcessInput @ 0x49738:84-97
int  PopUnlock(Game &g, int field);         // Game::unlockPopups (ShowUnlockMenu @ 0x671ec)

// ---- player name (NameScreen, task W6) ---------------------------------------------------------
// The name lives in Game+0xc1f8 (std::string, max 16 chars; in the run save, persists across
// games — NewGame @ 0x2de68 shows NameScreen with the current name, InitNewGame never clears it).
void SetPlayerName(Game &g, const char *s);   // Play-button copy, ProcessInput @0x57a10:498-501
std::string GenerateRandomName(Game &g);      // NameScreen::GenerateRandomName @0x5615c (+ set)

// ---- wall clock (task W7) -----------------------------------------------------------------------
// Game::SetClockState @ 0x25038: writes +0x142f9 = on, +0x142fc = old ^ new, then forwards to
// Interface::SetClockState @ 0x487dc (UI: clock-quad +0xcac UVs + flag +0x11d4 + GameMenu quad —
// presentation, done by the page). The clock string is Java-side (wall time, "h:mm a").
void SetClockState(Game &g, bool on);
bool ClockEnabled(const Game &g);   // Game::IsClockEnabled: reads +0x142f9 (JNI wrapper @ 0x234dc)

// ---- high scores (ScoreScreen::AddScore @ 0x5cc5c) --------------------------------------------
struct ScoreEntry {
    char name[20] = {};          // entry+0x00: strcpy(Game+0xc1f8) at AddScore @0x5cc5c:96
    int cls = -1;                 // REBUILD-ADDED: AddScore has no class argument (see m8.cpp)
    int turn = -1;                // entry+0x14 (param_2)
    int col[3] = {-1, -1, -1};    // entry+0x18/+0x1c/+0x20 (param_3..param_5)
    int score = -1;               // entry+0x24 (param_6 = sort key); -1 = empty slot
    int skills[4] = {-1, -1, -1, -1};  // entry+0x28..+0x34 (ScoreScreen::SetSkills @ 0x5c398)
};

struct ScoreBook {
    static constexpr int PAGES = 6;   // ScoreType 0..5 (analysis/data/ENUMS.md)
    static constexpr int ROWS = 10;   // AddScore @ 0x5cc5c sorted insert, 10/page
    ScoreEntry e[PAGES][ROWS];
    // Insert the current run the way CheckEndGame @ 0x2f960 does; returns the 0-based rank,
    // or -1 when the caller's page is out of range.
    int Add(const Game &g, int page);
    const ScoreEntry *Get(int page, int rank) const;
    std::vector<uint8_t> Serialise() const;
    bool Deserialise(const uint8_t *bytes, int len);
};

} // namespace dr
