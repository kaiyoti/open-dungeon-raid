// Misc: the shared token-collection entry point and the two racial abilities that every
// system calls. Bodies follow analysis/decomp/Game/{AddToken,Healing,Shapeshift} and
// analysis/progression/RESOURCES.md; constants re-read from the .so data segment.
#include "dr/game.h"
#include "dr/skill.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace dr {
namespace {

// Interface::GetSpecialTarget @ 0x4b254 — pick a skill for a mana potion to recharge:
// any skill that is not the "Mana" skill itself (type 29) and not fully charged;
// random index among them (RandI stream 5), -1 when none (the token then dies unused).
int ManaTarget(const Game &g)
{
    std::vector<int> targets;
    for (int i = 0; i < static_cast<int>(g.skills.size()); ++i)
        if (g.skills[i]->type != SKILL_MANA && g.skills[i]->charge != g.skills[i]->maxCharge)
            targets.push_back(i);
    if (targets.empty()) return -1;
    return targets[static_cast<std::size_t>(RandI(0, static_cast<int>(targets.size()) - 1, RAND_OTHER))];
}

// K12 — token flight: how many frames after the release a token takes to reach its HUD target.
// Game::Process @0x339ac runs Token::Process @0x65200 every frame with dt = Game+4 (asm at the
// call site: `ldr r1,[r4,#4]`, 0x23cdc):
//   speed += dt*accel;
//   if speed <= 1.0 (DAT_00065330): step = dt*scale, move = (1-speed)*dir*step + speed*unit*step;
//   else                            step = speed*scale*dt, move = unit*step;
//   land (token+0x72 = 0) when step > distance — i.e. the frame returned here, counted from the
//   release frame (frame 1: the release frame itself, where the token loop already runs).
// The params come from Token::InitialiseFromTile @0x653d0: speed0 = DAT_000655f0 = 0, dir =
// Rand(-0.1,0.1)^2 normalised to length DAT_000655f4 = 0.1 (:64-76), scale = Rand(6,7) (:78),
// accel = Rand(1.0,1.35) (:81, literals 0x3f800000/0x3faccccd; K14 measured on the original:
// +0x90 = 1.0 + 0.35*u[3] for every token, c7 s7 m52) — the first four of the five stream-0
// draws CosmeticToken consumes; G5b: the scale is the Rand(4,6) re-draw @:111 (lr=0x655c4)
// when u[4] >= 0 (mode!=3 && tokenType == 1, skull kills only), else Rand(6,7) @:78.
// Geometry measured on the original (tools/emu/k12_geom.py, seeds 7 m52 and 10 m61): the tile
// grid is x = (1 + (col+0.5)*53)/320, y = (79 + (row+0.5)*53)/320 in the normalised (width = 1)
// screen space; targets are Interface::GetTarget @0x49028 float2s.
int TokenFlight(float sx, float sy, float tx, float ty,
                float dirx, float diry, float scale, float accel)
{
    const float dt = 1.0f / 30.0f;   // harness frame: tools/emu/game.py FRAME_US = 33333
    float px = sx, py = sy, speed = 0.0f;
    for (int frame = 1; frame <= 600; ++frame) {
        float dx = tx - px, dy = ty - py;
        float dist = std::sqrt(dx * dx + dy * dy);
        speed += dt * accel;
        float ux = dist > 0.0f ? dx / dist : 0.0f;
        float uy = dist > 0.0f ? dy / dist : 0.0f;
        float step;
        if (speed <= 1.0f) {
            step = dt * scale;
            float rem = 1.0f - speed;
            float mx = rem * step * dirx + speed * step * ux;
            float my = rem * step * diry + speed * step * uy;
            if (step > dist) return frame;      // decomp: `if (step <= dist) move` else land
            px += mx; py += my;
        } else {
            step = speed * scale * dt;
            if (step > dist) return frame;
            px += step * ux; py += step * uy;
        }
    }
    return 600;
}

} // namespace

// Game::AddToken @ 0x2a934 — a cleared tile drops one resource token. The original pushes a
// Token quad onto the +0xb8d4 vector (Token::InitialiseFromTile @ 0x653d0), animates it to the
// HUD, and Game::Process @ 0x339ac calls AddResource(type, tokenType, value) when it lands.
// Headless: queue it with the frame at which it lands.
// K12: `posTile` = the tile the token flies FROM. Native InitialiseFromTile @0x653d0:36-40 does
// Quad::CopyFrom(tile) then overwrites +0x54/+0x58 from the pos argument, and MatchTile @0x2ac40
// passes the KILLED tile's screen position while its tile argument is board[0][0] — so the loot
// call site passes the killed tile, everything else passes nullptr (= flies from `t`).
void Game::AddToken(Tile &t, TileType type, bool bonus, const Tile *posTile)
{
    const Tile &src = posTile ? *posTile : t;

    int tokenType = 0;   // Token+0x7c (RESOURCES.md: 0 normal, 1 mana/skill-charge, 2 acid)
    int value = 0;       // Token+0x98 (mana potion: the skill index to recharge)
    bool collected = true;
    if (t.type == TILE_POTION) {                       // InitialiseFromTile checks the tile's own type
        if (t.manaPotion) {
            tokenType = 1;
            value = ManaTarget(*this);
            if (value < 0) collected = false;          // no skill to charge: token dies, no AddResource
        } else if (t.acidPotion) {
            tokenType = 2;
        }
    }
    // V24 + K2: the original animates the token at spawn — Token::InitialiseFromTile @ 0x653d0
    // consumes 4-5 stream-0 draws (AddToken @ 0x2a934:34 passes tokenType=type, tileType=t.type,
    // mode=0) — EXCEPT a mana potion with no recharge target: InitialiseFromTile @0x653d0:55-58
    // returns (token+0x72=0) before the first Rand at :64, so a dead mana token draws nothing.
    int flight = 1;      // K12: frames after release until this token lands (1 = the release frame)
    float u[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};   // A4b: stream-0 flight draws (cosmetic_draws.h)
    if (collected) {
        CosmeticToken(static_cast<int>(type), static_cast<int>(t.type), 0, u);
        // K12: only SHIELD tokens are modelled — their target is the armour/upgrade-point counter
        // (Interface::UpdateShieldTarget @0x48f3c), everything else flies to a moving bar head
        // (UpdateGoldBar @0x4a1dc / UpdateXPBar @0x49e34) which is UNKNOWN here, so those keep
        // flight = 1 (the V23 order: they land before the flame label).
        if (type == TILE_SHIELD && tokenType != 1) {
            float tx, ty;
            if (armour < maxArmour && GetEnemyCount(ENEMY_CORROSIVE) == 0) {
                tx = 159.0f / 320.0f;  ty = 447.0f / 320.0f;   // DAT_00048fa0 / DAT_00048fa4
            } else {
                tx = 0.1f + 110.0f / 320.0f;  ty = 450.0f / 320.0f;  // +0x1b8 + DAT_00048f98/9c
            }
            const int idx = static_cast<int>(&src - static_cast<const Tile *>(&board[0][0]));
            const float sx = (1.0f + (idx / BOARD_SIZE + 0.5f) * 53.0f) / 320.0f;
            const float sy = (79.0f + (idx % BOARD_SIZE + 0.5f) * 53.0f) / 320.0f;
            float dx = -0.1f + 0.2f * u[0], dy = -0.1f + 0.2f * u[1];   // Rand(-0.1,0.1) @:64/:66
            const float len = std::sqrt(dx * dx + dy * dy);
            if (dx == 0.0f && dy == 0.0f) dx = 0.1f;                    // @0x653d0:69-71
            else { dx = dx / len * 0.1f; dy = dy / len * 0.1f; }        // DAT_000655f4 = 0.1
            flight = TokenFlight(sx, sy, tx, ty, dx, dy,
                                 u[4] < 0.0f ? 6.0f + u[2]         // Rand(6,7) @:78
                                             : 4.0f + 2.0f * u[4], // C1g: Rand(4,6) re-draw @:111
                                 1.0f + 0.35f * u[3]);  // Rand(1.0,1.35) @:81 (literals
                                                        // 0x3f800000/0x3faccccd), Rand @0x67650
                                                        // = lo + (hi-lo)*r  (K14)
        }
    }

    // V23: the original lands the token later (Game::Process @ 0x339ac token loop); queue it.
    if (collected)
        tokenQueue.push_back({static_cast<int>(type), tokenType, value, flight});
    fx().TokenQueued(src, static_cast<int>(type), tokenType, collected ? value : -1, bonus,
                     flight, collected ? u : nullptr);   // M9 event hook (A3; A4b adds u5),
                                                          // presentation only — Game::AddToken @ 0x2a934
    fx().TokenAdded(t, static_cast<int>(type), tokenType, collected ? value : -1, bonus);  // V29 harness trace hook (drcli DR_TOKEN_TRACE), no game effect

    // HelpMenu::Initialise (this+0x103a0) tutorial popups at the tile, ids from the switch.
    // AddToken @0x2a934:84-90 also computes VectorSignedToFloat((+0xc1d4 + +0xc1d0) * (1|2)) and
    // ceilf(token value) inside the sprint block; their results are never stored (dead in the
    // decomp) — not rebuilt (K14).
    switch (type) {
    case TILE_POTION:
        fx().ShowHelp(3, &t);
        break;
    case TILE_SKULL:
        fx().ShowHelp(6, &t);
        if (bonus) ++killsCollectedBonus;     // +0x15e50
        else       ++killsCollected;          // +0x15e3c
        break;
    case TILE_COIN:
        fx().ShowHelp(2, &t);
        if (bonus) ++coinsCollectedBonus;     // +0x15e54
        else       ++coinsCollected;          // +0x15e40
        break;
    case TILE_SHIELD:
        fx().ShowHelp(5, &t);
        if (!t.broken) {                      // +0x26c: broken shields are not counted
            if (bonus) ++shieldsCollectedBonus;   // +0x15e58
            else       ++shieldsCollected;        // +0x15e44
        }
        break;
    default:
        break;                                // swords (4) and specials: no help, no counter
    }

    // Dungeon Sprint score (challenge && mode == MODE_SPRINT, the m=2 mode of RESOURCES.md):
    // collectedThisTurn/collectedScratch (+0x15e30/+0x15e34) = weighted totals of everything
    // collected so far this run. Normal shields first fill the armour deficit (maxArmour-armour);
    // the surplus scores on the normal side, the shortfall is charged to the bonus side.
    if (challenge && mode == MODE_SPRINT) {
        const int s = shieldsCollected - (maxArmour - armour);
        const int shieldsNormal = std::max(s, 0);
        const int shieldsBonus  = std::max(shieldsCollectedBonus + std::min(s, 0), 0);
        const float shieldW = (upRate + 1.0f)   * (doubleShields ? 2.0f : 1.0f);   // +0xc1a4, +0xc235
        const float goldW   = (goldRate + 1.0f) * (doubleCoins ? 2.0f : 1.0f);     // +0xc19c, +0xc237
        const float xpW     = xpRate + 1.0f;                                       // +0xc194
        collectedThisTurn = static_cast<int>(shieldsNormal * shieldW +
                                             killsCollected * xpW + coinsCollected * goldW);
        collectedScratch  = static_cast<int>(shieldsBonus * shieldW +
                                             killsCollectedBonus * xpW + coinsCollectedBonus * goldW);
    }
}

// Game::Healing @ 0x28c0c — Faun racial heal (RESOURCES.md): set HP to 25% of max HP
// (DAT_00028c88 = 0.25f, read from the .so), only when current HP is below that target.
// Achievement 0x32 "I Ain't Dead Yet" progress 5.0 (0x40a00000).
void Game::Healing()
{
    const int target = static_cast<int>(MaxHp() * 0.25f);   // (baseMaxHp + bonusMaxHp) * 0.25
    if (hp < target) {
        hp = target;                                        // +0xc158
        fx().QueueAchievement(ACH_AINT_DEAD_YET, 5.0f);
        fx().OnHealthChanged(hp);                           // UpdateHealthBar (UI cache not modelled)
    }
}

// Game::Shapeshift @ 0x24f78 — Changeling racial (chain.cpp calls it on every kill):
// fully recharge one random skill (charge +0x90 = maxCharge +0x8c).
void Game::Shapeshift()
{
    if (skills.empty()) return;                             // count +0xc368
    Skill &s = *skills[static_cast<std::size_t>(
        RandI(0, static_cast<int>(skills.size()) - 1, RAND_OTHER))];   // RandI(0, n-1, 5)
    s.charge = s.maxCharge;
    fx().OnSkillsChanged();                                 // Interface::UpdateSkills
}

} // namespace dr
