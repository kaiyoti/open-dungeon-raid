// A3 node test: play moves through dr.js and print the M9 event list for one
// sword-kills-enemy move. Sanity: matched tiles = the chain, falls consistent with the
// board after. TEST HARNESS code only — the move policy and menu picks are scaffolding,
// not game logic (game logic lives in rebuild/src, cited from the decomp).
//   node test_a3_events.js [seed]
'use strict';
const path = require('path');

function fail(msg) { console.error('FAIL: ' + msg); process.exit(1); }

function xorshift(s) {
  let x = s >>> 0 || 1;
  return () => { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; };
}

const KINDS = ['CHAIN_TILE', 'TILE_MATCHED', 'TILE_FELL', 'TOKEN', 'LABEL', 'PARTICLES',
  'FLARE', 'ATTACKER', 'INCOMING', 'SHIELD_BREAK', 'HEALTH', 'ARMOUR', 'MENU', 'SOUND',
  'PHASE_ATTACK'];

(async () => {
  const factory = require(path.join(__dirname, 'dr.js'));
  const M = await factory();
  const w = (ret, args, name) => M.cwrap(name, ret, args);
  const api = {
    init: w(null, [], 'dr_init'),
    new: w(null, ['number', 'number'], 'dr_new'),
    tile: w('number', ['number', 'number'], 'dr_tile'),
    buf: w('number', ['number'], 'dr_buf'),
    play: w('number', ['number', 'number'], 'dr_play'),
    menu: w('number', [], 'dr_menu'),
    offer: w('number', ['number', 'number'], 'dr_menu_offer'),
    choose: w(null, ['number', 'number'], 'dr_choose'),
    over: w('number', [], 'dr_over'),
    poll: w('number', [], 'dr_poll_sound'),
    evCount: w('number', [], 'dr_events_count'),
    ev: w('number', ['number', 'number'], 'dr_event'),
  };
  api.init();

  const rnd = xorshift((+process.argv[2] || 7) >>> 0);

  function randomPath() {  // harness move policy (same shape as test_headless.js)
    const t0 = (rnd() * 6) | 0, r0 = (rnd() * 6) | 0;
    const t = api.tile(t0, r0) & 7;
    if (t === 7) return null;
    const path = [{ c: t0, r: r0 }];
    const seen = new Set([t0 + r0 * 6]);
    for (;;) {
      if (path.length >= 3 + ((rnd() * 5) | 0)) break;
      const last = path[path.length - 1];
      const cand = [];
      for (let dc = -1; dc <= 1; dc++) for (let dr = -1; dr <= 1; dr++) {
        if (!dc && !dr) continue;
        const c = last.c + dc, r = last.r + dr;
        if (c < 0 || c > 5 || r < 0 || r > 5) continue;
        const id = c + r * 6;
        if (seen.has(id)) continue;
        const tt = api.tile(c, r) & 7;
        const ok = tt === t || (t === 1 && tt === 4) || (t === 4 && tt === 1);
        if (ok) cand.push({ c, r, id });
      }
      if (!cand.length) break;
      const pick = cand[(rnd() * cand.length) | 0];
      path.push({ c: pick.c, r: pick.r });
      seen.add(pick.id);
    }
    return path.length >= 3 ? path : null;
  }

  function readEvents() {
    const n = api.evCount(), out = [];
    for (let i = 0; i < n; i++) out.push([0, 1, 2, 3, 4, 5, 6, 7].map(f => api.ev(i, f)));
    return out;
  }

  function printEvents(evs) {
    evs.forEach((e, i) => {
      const [kind, phase] = e;
      console.log(`  #${String(i).padStart(2)} ph${phase} ${(KINDS[kind] || kind).padEnd(13)} ${e.slice(2).join(' ')}`);
    });
  }

  function sanity(evs, chain) {
    // matched tiles = the chain MINUS the enemies that survived the chain hit (MatchTile
    // @0x2ac40 clears every resource tile; AttackSkull @0x2f630 matches a skull only on death)
    const chainKeys = new Set(chain.map(c => c.c + c.r * 6));
    const matched = evs.filter(e => e[0] === 1);
    for (const m of matched)
      if (!chainKeys.has(m[2] + m[3] * 6)) fail(`matched tile (${m[2]},${m[3]}) not in the chain`);
    for (const c of chain) {
      const t = api.tile(c.c, c.r) & 7;   // post-move: a cleared tile is a refill, a survivor keeps its type
      const inMatched = matched.some(m => m[2] === c.c && m[3] === c.r);
      if (t !== 1 && !inMatched) fail(`chain tile (${c.c},${c.r}) type ${t} survived — a non-skull chain tile must clear`);
    }
    // falls: every matched cell is replaced by exactly one fell event (shift or spawn);
    // shifts move down (fromRow > toRow), spawns start above the board (fromRow < 0).
    const byCol = new Map();
    for (const f of evs.filter(e => e[0] === 2)) {
      const [, , col, fromRow, toRow, spawned] = f;
      if (!byCol.has(col)) byCol.set(col, []);
      byCol.get(col).push({ fromRow, toRow, spawned });
      if (spawned === 1 && fromRow >= 0) fail('spawn with fromRow >= 0');
      // rows grow downward: a shifted tile falls from a smaller row to the matched cell below
      if (spawned === 0 && fromRow >= toRow) fail(`shift not downward: col ${col} ${fromRow}->${toRow}`);
    }
    // gravity cascades: every cell at or above the TOP matched row of a column is refilled,
    // so fall targets = rows 0..maxMatchedRow and spawns = matched count (MoveMatchedTiles @0x2fd7c)
    const matchedByCol = new Map();
    for (const m of matched) {
      if (!matchedByCol.has(m[2])) matchedByCol.set(m[2], []);
      matchedByCol.get(m[2]).push(m[3]);
    }
    for (const [col, rows] of matchedByCol) {
      const fl = byCol.get(col) || [];
      const fell = fl.map(f => f.toRow).sort((a, b) => a - b);
      const want = Array.from({ length: Math.max(...rows) + 1 }, (_, i) => i);
      if (JSON.stringify(fell) !== JSON.stringify(want))
        fail(`col ${col}: fall targets [${fell}] != 0..${Math.max(...rows)}`);
      const spawns = fl.filter(f => f.spawned === 1).length;
      if (spawns !== rows.length) fail(`col ${col}: ${spawns} spawns != ${rows.length} matched`);
    }
    console.log(`sanity OK: ${matched.length} matched = chain, falls cover every matched cell`);
  }

  api.new(7, 1);
  while (api.poll() >= 0) {}

  let shown = 0;
  for (let move = 0; move < 400 && shown < 1; move++) {
    if (api.over()) break;
    const p = randomPath();
    if (!p) continue;
    const hasSword = p.some(c => (api.tile(c.c, c.r) & 7) === 4);
    const bytes = new Uint8Array(p.map(c => c.c + c.r * 6));
    const ptr = api.buf(bytes.length);          // packed cells go through the wasm staging buffer
    M.HEAPU8.set(bytes, ptr);
    if (!api.play(ptr, bytes.length)) continue;
    const evs = readEvents();
    const killedSkull = evs.some(e => e[0] === 1 && e[4] === 1);  // TILE_MATCHED tileType skull
    if (hasSword && killedSkull) {
      console.log(`sword-kills-enemy move ${move + 1}: chain ` +
        p.map(c => `${c.c},${c.r}`).join(' '));
      printEvents(evs);
      sanity(evs, p);
      shown++;
    }
    while (api.menu() >= 0) { api.choose(0, -1); while (api.poll() >= 0) {} }
    while (api.poll() >= 0) {}
  }
  if (!shown) fail('no sword-kills-enemy move found in 400 moves');
  console.log('A3 events test PASS');
})();
