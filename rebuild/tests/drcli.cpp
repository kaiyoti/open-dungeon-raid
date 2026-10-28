// Differential-test driver for the reconstruction (pairs with tools/emu/diff.py).
//   drcli <seed> <turns> [policySeed] [--diff N] [--mode sprint|pretzel] [--skills]
//        [--class N --class-level L] [--tap-trophy]
// Starts a new game with time(0) replaced by <seed>, then plays up to <turns> moves chosen by the
// random legal-move policy. Prints one line per state:
//   S <hex save>                      the initial state
//   M c,r c,r ...  then  S <hex save> each move and the state after it
//   T c,r                            C3: a trophy TAP instead of a chain (Game::TapTile — one
//                                     cell only; the tap does NOT advance the turn, so tools/emu
//                                     diff.py's turn==move-index check must skip T moves)
//   C <menu> <choice>                 a reward menu opened during that move (after its S line)
//   K <slot>                          --skills only: a skill activated BEFORE that move (the C
//                                     lines of a menu the activation opened follow the K line)
//   X <hex>                           --classes only (H3, TEST HARNESS): the all-10-classes blob
//                                     (Game::SerialiseClasses @ 0x24d98) after each S line
//   END over=<0|1>
// A move line given on stdin as "M c,r c,r ..." replaces the policy (replay mode: drcli <seed> -);
// replay lines may also be "T c,r" to replay a trophy tap (Task C3). Menu choice rule (same as
// tools/emu/game.py resolve_menu): always the first option (choice 0); level-up taps options 0
// AND 1. After a move that opened a menu, drcli prints the C line and applies the choice via
// Game::ChooseMenu (menus.cpp) so both sides keep playing with the same state.
// --tap-trophy (HARNESS, task C3): the move policy taps the first trophy on the board (fixed
// col-then-row scan) instead of playing a chain. Off by default so the M4 regression seeds stay
// on the untouched chain-only policy; only useful together with a native tap helper in
// tools/emu (master to add — see tasks/C3/RESULT.md).
#include "policy.h"
#include "dr/m8.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

using namespace drtest;

// Records the LAST menu opened during a move: the original re-runs FoldoutMenu::Initialise
// (Game+0x58bc) on every ShowMenu, so only the last type survives on the native side.
struct MenuLog : public Platform {
    int menu = -1;
    bool trace = std::getenv("DR_TOKEN_TRACE") != nullptr;   // V29 harness: print one T line per
    // Game::AddToken @ 0x2a934 (pairs with tools/emu/v29_probe.py). Test harness only.
    bool soundTrace = std::getenv("DR_SOUND_TRACE") != nullptr;   // S3 harness: print one "Q <id>"
    // line per Platform::QueueSound (the rebuild engine's sound queue order), pairs with
    // tools/emu/sound_id_log.py. Off by default -> diff.py regression output unchanged.
    bool labelTrace = std::getenv("DR_LABEL") != nullptr;   // C1 step 3 harness: print one
    // "L <kind> rise=<rise-speed draw>" line per Platform::AddLabel (the stream-0 rise Rand,
    // CosmeticLabel @ cosmetic_draws.cpp:34-42 = SimpleLabel::Initialise @0x5fbc4:27). The emu
    // rise for the F1b attack plan's health label (Game+0xbdbc) is 0.126 (fit from the capture's
    // label y-rise, tasks/F1b). Test harness only.
    void ShowMenu(MenuType m) override { menu = static_cast<int>(m); }
    void AddLabel(int labelKind, const Tile *t, int value) override {
        Platform::AddLabel(labelKind, t, value);   // sets labelRise (the rise draw, -1 = none)
        if (labelTrace) std::printf("L %d rise=%.4f\n", labelKind, labelRise);
    }
    void QueueSound(Sound s) override {
        if (soundTrace) std::printf("Q %d\n", static_cast<int>(s));
    }
    void TokenAdded(const Tile &t, int type, int tokenType, int value, bool bonus) override {
        if (trace) std::printf("T %d %d,%d tt=%d val=%d bonus=%d\n",
                               type, t.col, t.row, tokenType, value, bonus ? 1 : 0);
    }
};

static bool g_printClasses = false;   // H3 harness flag (see --classes in main)

static void PrintSave(Game &g)
{
    // K9 TEMP PROBE (test harness, not game logic): with DR_INCHAIN set, print the per-tile
    // inChain (+0x8a) grid after every state — pairs with tools/emu/k9_probe.py's native grid.
    if (std::getenv("DR_INCHAIN")) {
        std::printf("G");
        for (int c = 0; c < dr::BOARD_SIZE; ++c)
            for (int r = 0; r < dr::BOARD_SIZE; ++r) std::printf(" %d", g.board[c][r].inChain ? 1 : 0);
        std::printf(" hp=%d armour=%d\n", g.hp, g.armour);
    }
    std::printf("S ");
    for (uint8_t b : g.SerialiseState()) std::printf("%02x", b);
    std::printf("\n");
    // V24: stream-0 seed after this state (not in the save; diff.py compares it with the native
    // GetRandSeed(0)). Test harness output, not game logic.
    std::printf("R %u\n", dr::GetRandSeed(dr::RAND_COSMETIC));
    if (g_printClasses) {
        // H3 (TEST HARNESS code, not game logic): print the all-10-classes blob (Game::
        // SerialiseClasses @ 0x24d98: 0x00 0x0a + 10 x 47 B) after each S line. The native
        // side re-serialises on demand (tools/emu/game.py classes_save), so re-serialise here
        // too — the rebuild's classesSave (src/save.cpp:378) is only a snapshot of the last
        // in-game call (turn.cpp:355 / character_class.cpp:254).
        g.SerialiseClasses();
        std::printf("X ");
        for (uint8_t b : g.classesSave) std::printf("%02x", b);
        std::printf("\n");
    }
}

static void PrintMove(const std::vector<Game::Cell> &m)
{
    std::printf("M");
    for (auto &c : m) std::printf(" %d,%d", c.col, c.row);
    std::printf("\n");
}

int main(int argc, char **argv)
{
    // positional args: <seed> <turns|-> [policySeed]; optional flags --diff N, --mode sprint|
    // pretzel, --skills (V26: optional skill-use policy, harness — see policy.h ReadySkill),
    // --class N [--class-level L] (C1: run as class N — harness, mirrors tools/emu/game.py
    // select_class: Game::SetClassLevel @0x24d28 raise-only, then level 0 -> class 0 (locked,
    // Initialise @0x36524: classes 1..9 start at level 0), else activeClass = N, +0x15cd8 = 1).
    std::vector<std::string> pos;
    int diff = DIFF_NORMAL;
    std::string mode = "normal";
    bool useSkills = false;
    bool tapTrophy = false;          // C3 harness: policy taps trophies (see header comment)
    int classIdx = -1, classLevel = 0;
    bool hasClassLevel = false;
    bool preview = false;            // W3 harness: run the M8 drag preview before every move
    int nameBefore = 0, nameAfter = 0;  // W6 harness: RNG proof, see --name-before/--name-after
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--diff" && i + 1 < argc) diff = std::atoi(argv[++i]);
        else if (a == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (a == "--skills") useSkills = true;
        else if (a == "--tap-trophy") tapTrophy = true;
        else if (a == "--preview") preview = true;   // W3 harness: preview every chain (stderr report)
        else if (a == "--classes") g_printClasses = true;   // H3 harness: print "X <hex>" per state
        else if (a == "--class" && i + 1 < argc) classIdx = std::atoi(argv[++i]);
        else if (a == "--class-level" && i + 1 < argc) { hasClassLevel = true; classLevel = std::atoi(argv[++i]); }
        else if (a == "--name-before" && i + 1 < argc) nameBefore = std::atoi(argv[++i]);
        else if (a == "--name-after" && i + 1 < argc) nameAfter = std::atoi(argv[++i]);
        else pos.push_back(a);
    }
    if (pos.size() < 2) { std::fprintf(stderr, "usage: drcli <seed> <turns|-> [policySeed] [--diff N] [--mode sprint|pretzel] [--skills] [--tap-trophy] [--classes] [--preview] [--class N --class-level L] [--name-before N] [--name-after N]\n"); return 2; }
    if (hasClassLevel && classIdx < 0) {
        std::fprintf(stderr, "--class-level needs --class N\n"); return 2;
    }
    if (classIdx > 0 && !hasClassLevel) {
        // classes 1..9 start locked (level 0) and ClassScreen forces class 0 for a locked pick
        // (Process @0x3a598:506-511 / FadeOut @0x3730c), so a level must be granted first.
        std::fprintf(stderr, "class %d starts locked (level 0); pass --class-level L (1..10)\n", classIdx);
        return 2;
    }
    uint32_t seed = std::strtoul(pos[0].c_str(), nullptr, 0);
    bool replay = pos[1] == "-";
    int turns = replay ? 1 << 30 : std::atoi(pos[1].c_str());
    std::mt19937 pick(pos.size() > 2 ? std::strtoul(pos[2].c_str(), nullptr, 0) : seed);
    MenuLog menus;
    Game g(&menus); g.seedOverride = seed;
    if (classIdx >= CLASS_COUNT) { std::fprintf(stderr, "--class must be 0..9\n"); return 2; }
    if (classIdx >= 0) {
        // Harness setup, not game logic: grant the level raise-only (Game::SetClassLevel @0x24d28,
        // the original's only caller is the dead OpenFeint path) then commit the pick the way
        // ClassScreen does (SelectClass: Validate + locked->0 + activeClass + Game+0x15cd8 = 1).
        if (hasClassLevel) g.SetClassLevel(static_cast<ClassType>(classIdx), classLevel);
        g.SelectClass(classIdx);
    }
    if (mode == "normal") {
        // W6 RNG proof (TEST HARNESS, not game logic): --name-before N runs the real ported
        // GenerateRandomName (stream-0 draws, NameScreen @0x5615c) N times, then clears the
        // name so the save stays byte-identical to the emulator. In the original the draws sit
        // between the two InitNewGame calls (NewGame @0x2de68 -> NameScreen -> Play -> InitNewGame
        // again), so they cannot shift the started game — diff.py must stay identical.
        for (int i = 0; i < nameBefore; ++i) dr::GenerateRandomName(g);
        if (nameBefore > 0) g.playerName.clear();
        g.NewGame(static_cast<Difficulty>(diff));
    } else {
        g.difficulty = static_cast<Difficulty>(diff);
        g.challenge = true;
        g.mode = (mode == "sprint") ? MODE_SPRINT : MODE_PRETZEL;
        for (int i = 0; i < nameBefore; ++i) dr::GenerateRandomName(g);
        if (nameBefore > 0) g.playerName.clear();
        g.InitNewGame();   // NewGame() clears challenge; challenge modes call InitNewGame directly
    }
    PrintSave(g);
    // W6 (TEST HARNESS): --name-after N draws stream-0 the same way AFTER the game started —
    // unlike --name-before this MUST drift every later S/R line (MatchTile trophy spawn @0x2ac40
    // and other in-play stream-0 sites), proving the draws really touch the game RNG when taken
    // at the wrong moment (the original never does: the NameScreen is gone by then).
    for (int i = 0; i < nameAfter; ++i) dr::GenerateRandomName(g);
    if (nameAfter > 0) g.playerName.clear();
    std::string line;
    for (int i = 0; i < turns && !g.gameOver; ++i) {
        // V26: OPTIONAL skill-use policy (--skills, harness rule — see policy.h ReadySkill):
        // activate the first charged active skill BEFORE the move (the native taps the same
        // button first, see diff.py/game.py tap_skill), then resolve any menu that activation
        // opened (Enchant -> Game::Upgrade @0x251d4), mirroring the post-move menu handling.
        if (useSkills) {
            int slot = ReadySkill(g);
            if (slot >= 0) {
                std::printf("K %d\n", slot);
                g.ActivateSkill(slot);                    // ProcessInput @0x49738:84-97 equivalent
                // HARNESS: land the tokens the activation queued NOW — diff.py tap_skill settles
                // the native right after the tap, so its AddResource (and any menu that opens
                // when a bar crosses 99) happens before the move on both sides.
                std::vector<int> opened;
                for (;;) {
                    g.DrainTokens();
                    if (!g.menuPending) break;
                    opened.push_back(g.menuType);
                    g.ChooseMenu(0, g.menuType == MENU_LEVEL_UP ? 1 : -1);
                }
                for (int t : opened) std::printf("C %d 0\n", t);
            }
        }
        std::vector<Game::Cell> move;
        bool tap = false;                    // C3: this move is a trophy tap (T line), not a chain
        Game::Cell tapCell{0, 0};
        if (replay) {
            if (!std::getline(std::cin, line)) break;
            std::istringstream in(line); std::string tok; in >> tok;
            if (tok == "T") {                // "T c,r" — trophy tap (exactly one cell)
                tap = true;
                while (in >> tok) {
                    int c, r;
                    if (std::sscanf(tok.c_str(), "%d,%d", &c, &r) == 2) tapCell = {c, r};
                }
            } else {
                while (in >> tok) { int c, r; if (std::sscanf(tok.c_str(), "%d,%d", &c, &r) == 2) move.push_back({c, r}); }
            }
        } else if (tapTrophy) {
            // C3 harness move policy (NOT game logic): tap the first trophy on the board
            // (col-major scan, fixed order), else fall through to the chain policy.
            for (int c = 0; c < BOARD_SIZE && !tap; ++c)
                for (int r = 0; r < BOARD_SIZE && !tap; ++r) {
                    const Tile &t = g.At(c, r);
                    if (t.type == TILE_SPECIAL && t.specialVariant == SPECIAL_TILE_TROPHY) {
                        tap = true; tapCell = {c, r};
                    }
                }
            if (!tap) {
                move = FindMove(g, pick);
                if (move.empty() && !AnyMove(g, move)) break;
            }
        } else {
            move = FindMove(g, pick);
            if (move.empty() && !AnyMove(g, move)) break;
        }
        if (tap) std::printf("T %d,%d\n", tapCell.col, tapCell.row);
        else PrintMove(move);
        std::fprintf(stderr, "== move %d\n", i + 1);
        menus.menu = -1;
        if (preview && !tap && !move.empty()) {
            // W3 TEST HARNESS (not game logic): run the M8 drag preview
            // (dr_preview/dr_preview_tile, src/m8.cpp) over this move's chain and prove it left
            // neither the save nor the stream-0 RNG seed behind. Reported on stderr so the
            // stdout census lines diff.py compares stay byte-identical.
            const std::vector<uint8_t> before = g.SerialiseState();
            const uint32_t r0 = dr::GetRandSeed(dr::RAND_COSMETIC);
            const int atk = dr::PreviewChainAttack(g, move);
            for (const Game::Cell &c : move) {
                dr::PreviewDamageType(g, move, c.col, c.row);
                dr::PreviewWillDie(g, move, c.col, c.row);
            }
            const bool dirty = g.SerialiseState() != before ||
                               dr::GetRandSeed(dr::RAND_COSMETIC) != r0;
            std::fprintf(stderr, "== preview move %d atk=%d %s\n", i + 1, atk,
                         dirty ? "DIRTY" : "clean");
        }
        const bool took = tap ? g.TapTile(tapCell.col, tapCell.row)   // C3: trophy collection
                              : g.PlayMove(move);
        if (!took) { std::printf("INVALID\n"); break; }
        // V12: PlayMove defers the attack + EndOfTurn while a menu is pending; resolve every
        // queued menu BEFORE the save line (the native resolves, attacks, EndOfTurns, then saves).
        std::vector<int> opened;
        while (g.menuPending) {
            opened.push_back(g.menuType);
            // test-harness debug (not game logic): dump the offers when DR_OFFERS is set
            if (std::getenv("DR_OFFERS"))
                std::fprintf(stderr,
                             "OFFER move %d type=%d slot=%d skill=%d items=%d,%d,%d "
                             "hp=%d xp=%d gold=%d\n", i + 1, g.menuType, g.menuShopSkillSlot,
                             g.menuShopSkill ? g.menuShopSkill->type : -1,
                             g.menuShopItems[0] ? static_cast<int>(g.menuShopItems[0]->type) : -1,
                             g.menuShopItems[1] ? static_cast<int>(g.menuShopItems[1]->type) : -1,
                             g.menuShopItems[2] ? static_cast<int>(g.menuShopItems[2]->type) : -1,
                             g.hp, g.xp, g.gold);
            g.ChooseMenu(0, g.menuType == MENU_LEVEL_UP ? 1 : -1);
        }
        if (std::getenv("DR_OFFERS"))
            std::fprintf(stderr, "AFTER move %d hp=%d xp=%d gold=%d armour=%d\n",
                         i + 1, g.hp, g.xp, g.gold, g.armour);
        PrintSave(g);
        for (int t : opened) std::printf("C %d 0\n", t);   // choice 0 = first option (V3 rule)
    }
    std::printf("END over=%d\n", g.gameOver ? 1 : 0);
}
