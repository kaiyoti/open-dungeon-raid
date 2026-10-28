// Six-stream multiplicative RNG — exact reconstruction (analysis/state/RNG.md).
#pragma once
#include <cstdint>
#include "enums.h"

namespace dr {

constexpr unsigned RAND_ALL = RAND_TYPE_COUNT;   // SetRandSeed(seed, 6) seeds every stream

void     SetRandSeed(uint32_t seed, unsigned type);
void     FlushRandSeeds();
uint32_t GetRandSeed(unsigned type);
float    RandUnitF(uint32_t &seed);              // uniform [0,1), 22-bit
float    Rand(float a, float b, RandType type);  // a + (b-a)*u
int      RandI(int lo, int hi, RandType type);   // uniform integer in [lo, hi] inclusive (bounds may be swapped)

void     DrSetRngTrace(int on);                  // TEST HARNESS toggle for the R/I stderr trace

} // namespace dr
