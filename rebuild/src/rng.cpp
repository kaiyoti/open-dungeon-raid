// Reconstructed RNG (SetRandSeed/FlushRandSeeds/GetRandSeed/RandUnitF/Rand/RandI).
// See analysis/state/RNG.md. Behaviour matches the ARM code bit-for-bit for u.
#include "dr/rng.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dr {

static uint32_t g_seeds[6];
static int rngTrace = -1;
static bool traceOn() { if (rngTrace < 0) rngTrace = getenv("DR_RNG_TRACE") ? 1 : 0; return rngTrace; }

// C1 step 3 TEST HARNESS (not game logic): web builds have no env, so the page probe
// (tasks/C1/dbg_c1_page.py) flips the existing DR_RNG_TRACE print via this setter.
void DrSetRngTrace(int on) { rngTrace = on; }

void SetRandSeed(uint32_t seed, unsigned type)
{
    if (type == RAND_ALL) for (uint32_t &s : g_seeds) s = seed;
    else if (type < 6) g_seeds[type] = seed;
}

void FlushRandSeeds() { for (uint32_t &s : g_seeds) s *= 16807u; }

uint32_t GetRandSeed(unsigned type) { return type < 6 ? g_seeds[type] : 0; }

float RandUnitF(uint32_t &seed)
{
    seed *= 16807u;                                   // mod 2^32
    uint32_t bits = (seed >> 10) | 0x40000000u;       // float in [2, 3)
    float f; std::memcpy(&f, &bits, sizeof f);
    float u = f - 2.0f;
    return u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);   // Clamp(u, 0, 1)
}

float Rand(float a, float b, RandType type)
{
    uint32_t s0 = g_seeds[type];
    float r = a + (b - a) * RandUnitF(g_seeds[type]);
    if (traceOn()) fprintf(stderr, "R%d %.7g %.7g -> %.7g (seed %08x)\n", (int)type, a, b, r, s0);
    return r;
}

int RandI(int lo, int hi, RandType type)
{
    int r;
    uint32_t s0 = g_seeds[type];
    if (hi < lo) { int n = lo + 1 - hi; float u = RandUnitF(g_seeds[type]); r = (int)floorf(hi + n * u); }
    else { int n = hi - lo + 1; float u = RandUnitF(g_seeds[type]); r = (int)floorf(lo + n * u); }
    if (traceOn()) fprintf(stderr, "I%d %d %d -> %d (seed %08x)\n", (int)type, lo, hi, r, s0);
    return r;
}

} // namespace dr
