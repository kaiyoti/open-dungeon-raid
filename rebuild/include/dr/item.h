// Equipment item. Original: class Item (derives Upgrade), 0x204 bytes; analysis/progression/ITEM_SKETCH.md,
// ITEM_STATS.md. Stats are a fixed array indexed by item stat id (20 slots, serialised as a 20-bit mask).
#pragma once
#include <array>
#include <string>
#include <vector>
#include "enums.h"

namespace dr {

class Game;
constexpr int ITEM_STAT_SLOTS = 20;

struct ItemUpgrade {               // class ItemUpgrade; ItemUpgrade::GetRandomStat @ 0x51db8
    int stat = -1;                 // +0x80 which item stat it raises
    int value = 0;
};

int ItemIdBase(ItemType t);   // low end of the type's id range (items.cpp)

struct Item {
    Game *game = nullptr;          // +0x7c
    ItemType type = WEAPON;
    int id = 0;                    // +0x84 index into the per-type item table (saved relative to the type's base)
    int nameIndex[2] = {-1, -1};   // +0x88 / +0x8c (prefix/name tables, 0xff = none)
    int label = 0;                 // +0x90
    std::array<int, ITEM_STAT_SLOTS> stats{};   // +0x114.. (40-int block, first 20 are stats)
    std::vector<ItemUpgrade> upgrades;

    void Initialise(ItemType t, const Item *replacing);   // Item::Initialise / GenerateStats (R tasks)
    void AddUpgrade(const ItemUpgrade &u);
};

} // namespace dr
