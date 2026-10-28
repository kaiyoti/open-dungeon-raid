// A4a node test: the falling-tile + floating-label physics in template.html's FX block vs the
// measured animation captures of the ORIGINAL (analysis/anim/CAPTURES.md §1/§3 and
// analysis/anim/captures/*.json, measured frame by frame by tools/emu/anim_capture.py).
// Checks the falling y(t) per row 0..5 and the frames-to-land for every measured case.
// TEST HARNESS code only — not game logic (the physics constants live in template.html's FX
// block, cited from the decomp there).
'use strict';
const fs = require('fs');
const path = require('path');

function fail(msg) { console.error('FAIL: ' + msg); process.exit(1); }

// extract the pure FX block (the <script> that defines it) and evaluate it
const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
const src = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1])
  .find(b => b.includes('A4a animation physics'));
if (!src) fail('FX block not found in template.html');
const mod = { exports: {} };
new Function('module', src)(mod);
const FX = mod.exports;
if (!FX || !FX.fallStep) fail('FX block did not export fallStep');

const TOL = 0.005;   // the game runs float32 (dt = 1/30 is inexact there); JS is float64

// --- CAPTURES.md §1: falling tiles, identical in all 4 captures --------------------------
// shieldbreak.json col 5 (8-cell chain: every row of the column falls; shifts come from the
// rows above, refills spawn above the board at fromRow < 0). Measured LAST AIRBORNE FRAME
// per (col, target row) from analysis/anim/captures/shieldbreak.json (the falling flag
// clears the frame after):
const col5 = [
  [-4, 0, 15], [-3, 1, 13], [-2, 2, 11], [-1, 3, 10], [0, 4, 10], [1, 5, 9],
];
for (const [fromRow, toRow, lastAir] of col5) {
  const traj = FX.fallTrajectory(FX.center(fromRow), FX.center(toRow), FX.fallAccel(toRow));
  const landFrame = traj.length - 1;   // traj[0] = frame 0 (one step ran in the release frame)
  if (landFrame !== lastAir + 1)
    fail(`col5 row ${toRow} from row ${fromRow}: lands frame ${landFrame}, measured ${lastAir + 1}`);
  const last = traj[traj.length - 1];
  if (!last.landed || Math.abs(last.y - FX.center(toRow)) > TOL)
    fail(`col5 row ${toRow}: must hard-stop at ty=${FX.center(toRow)}, got y=${last.y}`);
  if (last.v !== 1.0) fail(`col5 row ${toRow}: landing must reset v to 1.0, got ${last.v}`);
}

// CAPTURES.md §1 example y(t), cell (5,5), a = 4.8 (shieldbreak.json frames 0..9):
const y55 = [0.5340, 0.5780, 0.6273, 0.6820, 0.7420, 0.8073, 0.8780, 0.9540, 1.0353, 1.1220];
{
  const traj = FX.fallTrajectory(FX.center(1), FX.center(5), FX.fallAccel(5));
  y55.forEach((ym, f) => {
    if (Math.abs(traj[f].y - ym) > TOL)
      fail(`(5,5) frame ${f}: y=${traj[f].y}, measured ${ym}`);
  });
  if (!traj[10].landed || Math.abs(traj[10].y - 1.1578) > TOL)
    fail('(5,5): flag must clear frame 10 at y = 1.158 (CAPTURES §1)');
}

// CAPTURES.md §1 airborne times: "1-cell fall in row 0 (0.033 -> 0.330) = 8 frames; refill
// from y = -0.133 = 12 frames; from y = -0.299 = 16 frames; the same 0.63 distance in row 5
// (a = 4.8) = 10 frames".  Airborne frames = traj.length - 1 (frames 0..land-1).
const cases = [
  ['1-cell fall row 0 (attack.json 1,0)', FX.center(-2), FX.center(0), FX.fallAccel(0), 8],
  ['refill from y=-0.133 (coins.json 2,0)', FX.center(-3), FX.center(0), FX.fallAccel(0), 12],
  ['refill from y=-0.299 (shieldbreak 5,0)', FX.center(-4), FX.center(0), FX.fallAccel(0), 16],
  ['0.63 dist in row 5 (shieldbreak 5,5)', FX.center(1), FX.center(5), FX.fallAccel(5), 10],
];
for (const [name, y0, ty, a, want] of cases) {
  const traj = FX.fallTrajectory(y0, ty, a);
  if (traj.length - 1 !== want)
    fail(`${name}: airborne ${traj.length - 1} frames, measured ${want}`);
}

// measured frame-0 velocities: v0 = 1.0 + a*dt = 1.027, 1.053, 1.080, 1.107, 1.133, 1.160
// for rows 0..5 (CAPTURES §1) — one integration step has already run in the release frame.
const v0meas = [1.0267, 1.0533, 1.0800, 1.1067, 1.1333, 1.1600];
for (let row = 0; row < 6; row++) {
  const traj = FX.fallTrajectory(FX.center(row - 5), FX.center(row), FX.fallAccel(row));
  if (Math.abs(traj[0].v - v0meas[row]) > 0.001)
    fail(`row ${row} frame-0 v = ${traj[0].v}, measured ${v0meas[row]}`);
}

// --- CAPTURES.md §3: SimpleLabels ----------------------------------------------------------
// alpha = 255*(1 - age^2): measured 199 at age .467, 152 at .633, 16 at .967, 0 at 1.000;
// a fresh label starts at 255.
for (const [age, want] of [[0, 255], [0.467, 199], [0.633, 152], [0.967, 16], [1.0, 0]]) {
  const got = FX.labelAlpha(age);
  if (Math.abs(got - want) > 1) fail(`labelAlpha(${age}) = ${got}, measured ${want}`);
}
// rise: y 1.202 -> 1.102 over its 16 frames (0.19/s, CAPTURES §3)
const yEnd = FX.labelY(1.202, 16 / 30);
if (Math.abs(yEnd - 1.102) > 0.005) fail(`label rise over 16 frames: ${yEnd}, measured 1.102`);
// lifetime 1.0 s: dies the frame age >= life (SimpleLabel::Process @0x5fc64)
if (FX.LABEL_LIFE !== 1.0) fail('label lifetime must be 1.0 s');

console.log('A4a animation test PASS: falling y(t) rows 0..5 + frames-to-land vs CAPTURES §1, labels vs §3');
