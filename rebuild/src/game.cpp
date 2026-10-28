// Game construction and small helpers shared by all systems.
#include "dr/game.h"
#include "dr/character_class.h"
#include "dr/item.h"
#include "dr/skill.h"

namespace dr {

Game::Game(Platform *platform) : platform_(platform ? platform : &defaultPlatform_)
{
    for (int c = 0; c < BOARD_SIZE; ++c)
        for (int r = 0; r < BOARD_SIZE; ++r) { board[c][r].game = this; board[c][r].col = c; board[c][r].row = r; }
    // CharacterClass::Initialise for all 10 classes — the class half of Game::Start @ 0x260ac:168-172
    // (race, inherent perk/flaw, level 1 for class 0 / 0 = locked for 1..9, base skills, non-base
    // list, replacement vector). Body in src/character_class.cpp.
    for (int i = 0; i < CLASS_COUNT; ++i) {
        classes.push_back(std::make_unique<CharacterClass>());
        classes.back()->Initialise(i, this);
    }
}

Game::~Game() = default;

CharacterClass &Game::ActiveClass() { return *classes[activeClass]; }
bool Game::HavePerk(PerkType p) const { return classes[activeClass]->HavePerk(p); }
bool Game::HaveFlaw(FlawType f) const { return classes[activeClass]->HaveFlaw(f); }
RaceType Game::Race() const { return classes[activeClass]->race; }

} // namespace dr
