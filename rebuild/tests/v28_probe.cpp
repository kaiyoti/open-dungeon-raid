// V28 probe (test harness code, not game logic): Halfling race-6 15+-kill branch
// (ProcessInput @0x330c0:286-295). Builds the same state as tools/emu/v28_probe.py
// (NewGame(seed), class-0 race = 6, cols 0..2 filled with 1/0/1 normal enemies),
// plays the same 15-tile chain, prints PRE/POST armour stats[11] + save hexes.
#include "dr/game.h"
#include "dr/character_class.h"
#include <cstdio>
#include <cstdlib>

using namespace dr;

int main(int argc, char **argv)
{
    uint32_t seed = std::strtoul(argv[1], nullptr, 0);
    int race = argc > 2 ? std::atoi(argv[2]) : 6;
    int nk = argc > 3 ? std::atoi(argv[3]) : 15;
    Game g;
    g.seedOverride = seed;
    g.NewGame(DIFF_NORMAL);
    g.classes[0]->race = static_cast<RaceType>(race);   // +0x14db4 (class 0 + 0x8)

    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 6; ++r) {
            Tile &t = g.board[c][r];
            t.type = TILE_SKULL; t.specialVariant = 3;
            t.enemyType = ENEMY_NORMAL;
            t.attack = 1; t.defence = 0; t.health = 1; t.maxHealth = 1;
            t.countdown = 0;
            t.abilityFired = t.frozen = t.flaming = t.flamePulse = false;
            t.flameDamage = 0;
            t.magicSword = t.arrow = t.manaPotion = t.acidPotion = t.explosivePotion = false;
            t.broken = t.promoted = t.resurrectBlocked = false;
            t.poisonTurns = t.poison = t.poisonAddedThisTurn = 0;
            t.matched = false; t.inChain = false;
        }

    std::printf("PRE %d %d\n", g.items[ARMOUR]->stats[11], g.armour);
    std::printf("S ");
    for (uint8_t b : g.SerialiseState()) std::printf("%02x", b);
    std::printf("\n");

    static const int path[15][2] = {
        {0,0},{1,0},{2,0},{2,1},{1,1},{0,1},{0,2},{1,2},{2,2},{2,3},{1,3},{0,3},{0,4},{1,4},{2,4}
    };
    std::vector<Game::Cell> move;
    for (int i = 0; i < nk; ++i) move.push_back({path[i][0], path[i][1]});
    if (!g.PlayMove(move)) { std::printf("INVALID\n"); return 1; }
    while (g.menuPending) g.ChooseMenu(0, g.menuType == MENU_LEVEL_UP ? 1 : -1);
    std::printf("POST %d %d\n", g.items[ARMOUR]->stats[11], g.armour);
    std::printf("S ");
    for (uint8_t b : g.SerialiseState()) std::printf("%02x", b);
    std::printf("\n");
    return 0;
}
