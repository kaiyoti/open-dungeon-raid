// V24: RNG bookkeeping for stream 0 (RAND_COSMETIC). The original's presentation code consumes
// stream-0 draws (analysis/state/RNG_STREAM0.md); the headless rebuild must consume the SAME count
// in the same order, or stream 0 drifts and the trophy countdown (Game::MatchTile @ 0x2ac40:255
// RandI(20,30,0)) diverges. Each helper cites the original function; the draw counts come from the
// decomp. These functions only advance the stream-0 seed (values discarded) — no game logic.
#pragma once

namespace dr {

// Game::AddParticles @ 0x2838c -> Particles::InitialiseFromTile(3-arg) @ 0x5af68 dispatch:
// kind 0 -> InitialiseFromTile @ 0x5aaf8: 3 (0x5aaf8:82,83,87) + 10 loop iters x 3 (98,213,216) = 33.
// kind 1/2/3 -> InitialiseAtPosition @ 0x5a7d4: 1 (0x5a7d4:38) + 10 x 8 (55,58,63,66,71,74,86,89) = 81.
// kind >= 4 -> no Initialise* call: 0 draws.
void CosmeticParticles(int kind);

// Game::AddFlare @ 0x29894:121 Rand(0,365,0): 1 draw per flare, ALWAYS — A17/D6-U19: the top
// test @0x29894:30-32 is the std::vector growth check (1/29 mod 2^32 with sizeof(Quad)=0x74),
// both paths converge at LAB_000298d2 and execute the draw + count++; no flare is ever dropped.
void CosmeticFlare();

// SimpleLabel::Initialise @ 0x5fbc4:27 Rand(0.1,0.2,0): 1 draw when the label type is NOT in
// {1,3,8,9,10} (ARM 0x5fbf6..0x5fc0c: types 1/8 via the r3 test, 9/10 via the r2 test, 3 via beq).
// A8: `rise` (optional) receives the label's rise speed = Rand(0.1,0.2,0) (SimpleLabel::Initialise
// @ 0x5fbc4:27 -> +0x344), -1 for the types that return before the draw (1,3,8,9,10).
void CosmeticLabel(int type, float *rise = nullptr);

// Token::InitialiseFromTile @ 0x653d0 (G5b native RNG trace, seed 1 attack plan):
// 4 stream-0 draws ALWAYS: :64 dir.x Rand(-0.1,0.1), :66 dir.y Rand(-0.1,0.1),
// :78 scale Rand(6,7), :81 accel Rand(1.0,1.35). The 5th draw is branch-conditional:
//   (mode != 3 && tokenType == 1) -> Rand(4,6,0) scale re-draw @0x655c4 (:111)          [+1]
//   (mode == 3 || tokenType != 1) && tokenType != tileType -> Tile::GetTileUV
//     @ 0x62d60:5 RandI(0,7,0)  [+1]
//   (mode != 3 && tokenType != 1 && tokenType == tileType) -> NO 5th draw               [4]
// (C1g's "always 5" was wrong: the trace shows 4 draws for every coin-from-coin token and
// 5 only for skull kills; the decomp branch test at :96 is tokenType != 1, not != tileType.)
// K12: when `u5` is given the draws are also handed back — they are the token's flight
// parameters (dir x/y @:64/:66 Rand(-0.1,0.1), scale @:78 Rand(6,7), accel @:81 Rand(1.0,1.35)
// — literal 0x3faccccd; slot 4 = the scale re-draw Rand(4,6) @:111 when that branch ran,
// -1.0 when there is no scale re-draw (4-draw token, or the 5th draw was the UV RandI).
// The web renderer replays the flight from these (M9 TOKEN event fields 8..12,
// API_CONTRACT.md) — presentation only.
void CosmeticToken(int tokenType, int tileType, int mode, float *u5 = nullptr);

// Game::ProcessAttack @ 0x311b8: 1 draw at the impact transition (state 4->5 armour :178 /
// 4->6 no armour :170); with armour a second draw at state 5->6 (:251). 1 (no armour) / 2 (armour).
void CosmeticAttackImpact(bool hasArmour);

// Game::InitialiseFlares @ 0x2a548 (called by ProcessAttack at attack start): for every attacker
// (skull, not inChain, not frozen; lich-with-fuse skipped) it adds TWO white eye flares
// (GetEyePosition x2, AddFlare @ 0x29894:121) = 2 stream-0 draws per attacker.
void CosmeticAttackFlares(int attackers);

// Game::ProcessInput @ 0x330c0 release path, after MatchTiles: 1 draw each when the press preview
// counters are live: flaming :266 (+0x1469c>0), spiky :277 (+0x14a10>0), attack :315 (+0xc240!=0).
void CosmeticPreview(bool flamingPreview, bool spikyPreview, bool attackPreview);

// Game::UpdateCrossVisibility @ 0x2c5e8 (called on press ProcessInput @0x330c0:141 with the
// 1-tile list, and on every drag add ProcessTileSelection @0x2c8dc:384 gated 0 < +0xc23c):
// pushes one SimpleLabel per selected tile whose Tile::GetDamageType @0x644bc is 1 (label type
// 0x1e, :117) or 3 (type 0x23, :84); both types draw 1 stream-0 (not in Initialise's skip set
// {1,3,8,9,10} @0x5fbc4). n = labels for this call.
void CosmeticCrossVisibility(int n);

// Test-harness debug (not game logic): logs a stream-0 consumption site when DR_RNG0 is set.
void Rng0Trace(const char *site, int n);

} // namespace dr
