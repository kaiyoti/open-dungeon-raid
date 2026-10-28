// U6 node test (TEST HARNESS code — not game logic): the page letterbox rule must equal the
// native glViewport rule of Game::SetupRenderer @0x26470
// (analysis/decomp/Game/SetupRenderer@00026470.c):
//   wide (W/H >= DAT_00026590=2/3, lines 28-39): vw=(int)(H*2/3), vh=H,
//        vx=(int)((W-vw)*DAT_00026594=0.5), vy=0                      -> pillarbox
//   tall (W/H <  2/3, lines 40-52): vw=W, vh=(int)(W/(2/3)), vx=0,
//        vy=(int)((H-vh)*0.5)                                         -> letterbox, centered
//   glViewport(vx,vy,vw,vh) line 53. 1080x1920 -> (0,150,1080,1620): 150 px bars top+bottom.
// Checks: (1) u6Letterbox() extracted from template.html == the decompiled int math exactly;
// (2) the CSS width formula min(100vw, 66.6667vh) reproduces native vw within 1 px;
// (3) ORIGINAL_CSS carries the centering wiring (flex + margin auto + var(--u6W)).
'use strict';
const fs = require('fs');
const path = require('path');

function fail(msg) { console.error('FAIL: ' + msg); process.exit(1); }

const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');

// (1) extract + eval the page function
const m = html.match(/const U6_REF = [\s\S]*?\nfunction u6Letterbox\(vw, vh\) \{[\s\S]*?\n\}/);
if (!m) fail('u6Letterbox block not found in template.html');
const u6Letterbox = new Function(m[0] + '\nreturn u6Letterbox;')();

// native reference: the decompiled int arithmetic (positive values: (int) == trunc)
function nativeViewport(W, H) {
  const REF = 2 / 3, HALF = 0.5;            // DAT_00026590 / DAT_00026594
  if (!(W / H < REF)) {                     // lines 27-28: wide branch
    const vh = Math.trunc(H);
    const vw = Math.trunc(H * REF);
    return { x: Math.trunc((W - vw) * HALF), y: 0, w: vw, h: vh };
  }
  const vw = Math.trunc(W);                 // lines 41-51: tall branch
  const vh = Math.trunc(W / REF);
  return { x: 0, y: Math.trunc((H - vh) * HALF), w: vw, h: vh };
}

const sizes = [320, 480, 540, 720, 960, 1080, 1280, 1600, 1920, 2400];
let checked = 0;
for (const W of sizes) for (const H of sizes) {
  const a = u6Letterbox(W, H), b = nativeViewport(W, H);
  if (a.x !== b.x || a.y !== b.y || a.w !== b.w || a.h !== b.h) {
    fail(`${W}x${H}: page ${JSON.stringify(a)} != native ${JSON.stringify(b)}`);
  }
  // (2) CSS formula: width = min(100vw, 66.6667vh) must equal native vw within 1 px
  const cssW = Math.min(W, H * (200 / 3) / 100);
  if (Math.abs(cssW - b.w) > 1) fail(`${W}x${H}: css width ${cssW} vs native ${b.w}`);
  checked++;
}
// the gate viewport itself
const g = u6Letterbox(1080, 1920);
if (g.x !== 0 || g.y !== 150 || g.w !== 1080 || g.h !== 1620) {
  fail('1080x1920: expected (0,150,1080,1620), got ' + JSON.stringify(g));
}

// (3) ORIGINAL_CSS wiring (centering + box var + caps off)
for (const tok of [
  'body{padding:0}',
  '.app{max-width:none;min-height:100vh;min-height:100dvh;display:flex;flex-direction:column;justify-content:center}',  // U11: dvh added
  '#scrMenu,#scrNew,#scrMode,#scrClasses,#scrScores,#scrGame{width:var(--u6W, min(100vw,66.6667vh));max-width:none;margin:0 auto}',
  '#boardWrap,#hudBar,#topBar{max-width:none}',
]) {
  if (!html.includes(tok)) fail('ORIGINAL_CSS missing: ' + tok);
}
console.log(`U6 letterbox rule vs SetupRenderer @0x26470: PASS (${checked} viewports exact int match; 1080x1920 -> (0,150,1080,1620); CSS wiring present)`);
