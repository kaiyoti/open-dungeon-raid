// Unit differential driver for Tile::GenerateStats (pairs with tools/emu/unit_stats.py, task V6).
// One case per input line (11 ints):
//   turn diff enemy seed challenge mode x armour race flaw invis
// One output line per case (7 values):
//   attack defence health maxHealth countdown promoteCountdown randSeed1
// flaw is a bitmask: 1 = Sneaky (3), 2 = Zealous (8), 4 = Noisy (9).
// x = total XP (xp + xpHundreds); invis = 1 puts an extra Invisible skull on the board.
#include "dr/game.h"
#include "dr/character_class.h"
#include "dr/rng.h"
#include <cstdio>

using namespace dr;

int main()
{
    Game g;
    Tile &t = g.board[0][0];
    Tile &b = g.board[1][0];
    g.promotionEnabled = true;
    g.activeClass = 0;
    CharacterClass &c = *g.classes[0];

    int turn, diff, enemy, seed, challenge, mode, x, armour, race, flaw, invis;
    while (std::scanf("%d %d %d %d %d %d %d %d %d %d %d",
                      &turn, &diff, &enemy, &seed, &challenge, &mode, &x, &armour,
                      &race, &flaw, &invis) == 11) {
        g.turn = turn;
        g.difficulty = static_cast<Difficulty>(diff);
        g.challenge = challenge != 0;
        g.mode = static_cast<GameMode>(mode);
        g.xp = x % 100;
        g.xpHundreds = (x / 100) * 100;      // +0xc210 is the century part in XP units
        g.armour = armour;

        c.race = static_cast<RaceType>(race);
        c.flaws[0] = c.flaws[1] = c.flaws[2] = c.flaws[3] = FLAW_NONE;
        if (flaw & 1) c.flaws[0] = FLAW_SNEAKY;
        if (flaw & 2) c.flaws[1] = FLAW_ZEALOUS;
        if (flaw & 4) c.flaws[2] = FLAW_NOISY;

        t.type = TILE_SKULL;
        t.enemyType = static_cast<EnemyType>(enemy);
        t.countdown = 0;
        if (invis) { b.type = TILE_SKULL; b.enemyType = ENEMY_INVISIBLE; }
        else       { b.type = TILE_POTION; b.enemyType = ENEMY_NORMAL; }
        g.promoteCountdown = -777;

        SetRandSeed(static_cast<uint32_t>(seed), RAND_TILE);
        t.GenerateStats(turn, static_cast<Difficulty>(diff));
        std::printf("%d %d %d %d %d %d %u\n", t.attack, t.defence, t.health, t.maxHealth,
                    t.countdown, g.promoteCountdown, GetRandSeed(RAND_TILE));
    }
    return 0;
}
