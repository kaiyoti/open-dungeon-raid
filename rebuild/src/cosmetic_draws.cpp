// V24: stream-0 (RAND_COSMETIC) bookkeeping — see include/dr/cosmetic_draws.h and
// analysis/state/RNG_STREAM0.md. Every Rand/RandI call in the original advances the stream seed
// exactly once (one RandUnitF, seed *= 16807 — analysis/state/RNG.md), so consuming N draws is
// N calls to Rand(); the ranges do not affect the seed. Not game logic: no state is read/written.
#include "dr/cosmetic_draws.h"
#include "dr/rng.h"
#include <cstdio>
#include <cstdlib>

namespace dr {

// Test-harness debug (not game logic): DR_RNG0=1 logs every stream-0 consumption site.
static const bool trace = std::getenv("DR_RNG0") != nullptr;
void Rng0Trace(const char *site, int n)
{
    if (trace && n > 0) std::fprintf(stderr, "rng0 %s x%d\n", site, n);
}

void CosmeticParticles(int kind)
{
    int n = 0;
    if (kind == 0) n = 33;              // Particles::InitialiseFromTile @ 0x5aaf8 (3 + 10x3)
    else if (kind >= 1 && kind <= 3) n = 81;   // InitialiseAtPosition @ 0x5a7d4 (1 + 10x8)
    Rng0Trace(kind == 0 ? "particles0" : (n ? "particles123" : "particlesX"), n);
    for (int i = 0; i < n; ++i) Rand(0.0f, 1.0f, RAND_COSMETIC);
}

void CosmeticFlare()
{
    Rng0Trace("flare", 1);
    Rand(0.0f, 1.0f, RAND_COSMETIC);    // Game::AddFlare @ 0x29894:121 Rand(0,365,0)
}

void CosmeticLabel(int type, float *rise)
{
    if (rise) *rise = -1.0f;
    // SimpleLabel::Initialise @ 0x5fbc4: types 1,3,8,9,10 return before the Rand at 0x5fc1e.
    if (type == 1 || type == 3 || type == 8 || type == 9 || type == 10) { Rng0Trace("label-nodraw", 0); return; }
    if (trace) std::fprintf(stderr, "  [label type %d]\n", type);   // K4 temp probe
    Rng0Trace("label", 1);
    float u = Rand(0.0f, 1.0f, RAND_COSMETIC);   // Rand(0.1,0.2,0)
    if (rise) *rise = 0.1f + 0.1f * u;           // +0x344 rise speed, game units/s (A8)
}

void CosmeticToken(int tokenType, int tileType, int mode, float *u5)
{
    // G5b native RNG trace (tools/emu/rng_trace.py -a, seed 1 attack plan, Rand/RandI hooks
    // @0x67650/0x67bf4): 4 draws ALWAYS — dir.x Rand(-0.1,0.1) lr=0x6545e, dir.y lr=0x65472,
    // scale Rand(6,7) lr=0x654ae, accel Rand(1.0,1.35) lr=0x654cc (Token::InitialiseFromTile
    // @ 0x653d0:64,66,78,81). The 5th draw is BRANCH-CONDITIONAL (decomp @0x653d0:96-110):
    //   (mode != 3 && tokenType == 1) -> Rand(4,6,0) scale re-draw @0x655c4 (:111)  [+1]
    //   (mode == 3 || tokenType != 1) && tokenType != tileType -> Tile::GetTileUV
    //     @ 0x62d60:5 RandI(0,7,0)  [+1]   (0 occurrences in the seed-1 plan: every non-skull
    //     token spawned from its own tile type)
    //   (mode != 3 && tokenType != 1 && tokenType == tileType) -> NO 5th draw (4 total).
    // C1g's "always 5" over-drew stream 0 by 1 per coin-from-coin token (trace: model +9/+6/+15
    // draws on moves 1/4/5 = the 9/6/15 non-skull tokens; G5: tile art variants desynced 28/36).
    const bool redraw = (mode != 3 && tokenType == 1);   // :104/:111 -> Rand(4,6) @0x655c4
    const bool uv = !redraw && (tokenType != tileType);  // :96/:98 -> GetTileUV @0x62d60:5
    const int n = 4 + ((redraw || uv) ? 1 : 0);
    Rng0Trace("token", n);
    if (u5) u5[4] = -1.0f;                // -1 = no scale re-draw (4-draw token or UV branch)
    for (int i = 0; i < n; ++i) {
        float u = Rand(0.0f, 1.0f, RAND_COSMETIC);
        if (u5 && i < 4) u5[i] = u;       // K12/A4b: flight params (see cosmetic_draws.h)
        else if (u5) u5[i] = u;           // redraw value (only when redraw)
    }
}

void CosmeticAttackImpact(bool hasArmour)
{
    Rng0Trace(hasArmour ? "attack2" : "attack1", hasArmour ? 2 : 1);
    Rand(0.0f, 1.0f, RAND_COSMETIC);    // 0x311b8:170 (armour<1, state 4->6) / :178 (4->5)
    if (hasArmour)
        Rand(0.0f, 1.0f, RAND_COSMETIC);   // 0x311b8:251 state 5->6
}

void CosmeticAttackFlares(int attackers)
{
    int n = 2 * attackers;              // InitialiseFlares @ 0x2a548: 2 eye flares per attacker
    Rng0Trace("attackflares", n);
    for (int i = 0; i < n; ++i) Rand(0.0f, 1.0f, RAND_COSMETIC);
}

void CosmeticCrossVisibility(int n)
{
    Rng0Trace("crossvis", n);
    for (int i = 0; i < n; ++i) Rand(0.0f, 1.0f, RAND_COSMETIC);
}

void CosmeticPreview(bool flamingPreview, bool spikyPreview, bool attackPreview)
{
    int n = (flamingPreview ? 1 : 0) + (spikyPreview ? 1 : 0) + (attackPreview ? 1 : 0);
    Rng0Trace("preview", n);
    if (flamingPreview) Rand(0.0f, 1.0f, RAND_COSMETIC);   // 0x330c0:266
    if (spikyPreview)   Rand(0.0f, 1.0f, RAND_COSMETIC);   // 0x330c0:277
    if (attackPreview)  Rand(0.0f, 1.0f, RAND_COSMETIC);   // 0x330c0:315
}

} // namespace dr
