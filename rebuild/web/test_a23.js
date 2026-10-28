// A23 node test (test harness code only — not game logic): runs template.html's app script
// in a DOM shim against the REAL dr.js and checks the three A23 rules:
//  1. game over kills the save: dr_save reports size 0 once dr_over() is 1
//     (CheckEndGame @0x2f960:156-157 sets native save size +0x14308 = 0) and saveGame()
//     then DELETES the stored save (DungeonRaidSurfaceView.java:385-389 file.delete()),
//     so toMenu() leaves mmContinue disabled (D13 5.4/7.4).
//  2. dragging back onto an EARLIER chain tile truncates the chain to that tile
//     (ProcessTileSelection @0x2c8dc:163-213) — not just one tile.
//  3. frozen enemies in a chain are decided natively (covered by tests/unit_a23.cpp);
//     here we only check the web layer keeps repeating tiles out of the chain.
'use strict';
const fs = require('fs');
const path = require('path');

function fail(msg) { console.error('FAIL: ' + msg); process.exit(1); }

function el(tag) {
  const e = {
    tag, children: [], style: {}, hidden: true, textContent: '', innerHTML: '',
    type: '', className: '', disabled: false, width: 0, height: 0, clientWidth: 480,
    classList: {
      s: new Set(),
      add(c) { this.s.add(c); }, remove(c) { this.s.delete(c); },
      contains(c) { return this.s.has(c); },
      toggle(c, on) { on ? this.s.add(c) : this.s.delete(c); },
    },
    append(...kids) { for (const k of kids) e.children.push(k); },
    appendChild(k) { e.children.push(k); return k; },
    addEventListener() {}, setPointerCapture() {},
    getBoundingClientRect() { return { left: 0, top: 0, width: 480, height: 480 }; },
    querySelector() { return el('div'); },
    onclick: null,
  };
  return e;
}
const ctxStub = require('./canvas_stub').makeStrictCtx();
const byId = new Map();
const document = {
  addEventListener() {},   // A24: BACK/Escape keydown handler (template.html)
  currentScript: { src: 'x' },
  fonts: { add() {} },
  getElementById(id) {
    if (!byId.has(id)) byId.set(id, (id === 'board' || id === 'fxLayer' || id === 'dimLayer' || id === 'hudBar') ? Object.assign(el('canvas'), { getContext: () => ctxStub }) : el('div'));
    return byId.get(id);
  },
  createElement: el,
};
const localStorage = { m: new Map(), getItem(k) { return this.m.has(k) ? this.m.get(k) : null; },
  setItem(k, v) { this.m.set(k, v); }, removeItem(k) { this.m.delete(k); } };
const window = { addEventListener() {}, devicePixelRatio: 1 };
const performance = { now: () => 0 };
function xorshift(s) {
  let x = s >>> 0 || 1;
  return () => { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; };
}

const LS_KEY = 'tileRaidRebuildSaveV1';   // template.html:1188
const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
const fxMod = { exports: {} };
new Function('module', blocks.find(b => b.includes('A4a animation physics')))(fxMod);
const app = blocks[blocks.length - 1].replace(/__ART__/g, 'false').replace(/__RECREATED__/g, 'null');
const epilogue = `
;globalThis.__T = {
  api: () => api, M: () => M, newGame, saveGame, hasSave, toMenu, pathOk,
  setDrag: p => { drag = p ? { path: p, last: null } : null; },
  getDrag: () => (drag ? drag.path : null),
  dragTo: (x, y) => dragTo(x, y),
  cellXY: (c, r) => ({ x: (c + 0.5) * cell, y: (r + 0.5) * cell + boardDy }),
};`;

(async () => {
  const factory = require(path.join(__dirname, 'dr.js'));
  const DR = () => factory();
  const run = new Function('window', 'document', 'localStorage', 'performance', 'DR',
    'fetch', 'Audio', 'FontFace', 'FX', app + epilogue);
  run(window, document, localStorage, performance, DR,
    () => Promise.reject(new Error('no fetch in test')), function () {}, function () {}, fxMod.exports);
  await new Promise(r => setTimeout(r, 150));
  const T = globalThis.__T;
  if (!T) fail('epilogue did not run (app script threw in boot?)');
  const api = T.api(), HEAP = () => T.M().HEAPU8;
  const rnd = xorshift(0xA23A7);

  // ---- 2. multi-tile truncation on backtrack (ProcessTileSelection @0x2c8dc:163-213)
  T.newGame(0xA23, 1, 3, 0);
  function findChain(minLen) {   // DFS for a pathOk chain of >= minLen (page predicate = dr_path_ok)
    for (let c0 = 0; c0 < 6; c0++) for (let r0 = 0; r0 < 6; r0++) {
      const p = [{ c: c0, r: r0 }], seen = new Set([c0 + r0 * 6]);
      (function grow() {
        if (p.length >= minLen) return true;
        const last = p[p.length - 1], cand = [];
        for (let dc = -1; dc <= 1; dc++) for (let dr = -1; dr <= 1; dr++) {
          if (!dc && !dr) continue;
          const c = last.c + dc, r = last.r + dr;
          if (c < 0 || c > 5 || r < 0 || r > 5 || seen.has(c + r * 6)) continue;
          if (!T.pathOk([...p, { c, r }])) continue;
          cand.push({ c, r });
        }
        cand.sort((a, b) => rnd() - 0.5);
        for (const q of cand) {
          p.push(q); seen.add(q.c + q.r * 6);
          if (grow()) return true;
          p.pop(); seen.delete(q.c + q.r * 6);
        }
        return false;
      })();
      if (p.length >= minLen) return p;
    }
    return null;
  }
  const chain = findChain(5);
  if (!chain) fail('no 5-tile legal chain on seed 0xA23 (board scan bug?)');
  const xy = (q) => T.cellXY(q.c, q.r);
  T.setDrag(chain.slice());
  let q = xy(chain[0]); T.dragTo(q.x, q.y);          // back to the FIRST tile
  if (T.getDrag().length !== 1) fail('truncate to first tile: len ' + T.getDrag().length + ', want 1');
  T.setDrag(chain.slice());
  q = xy(chain[1]); T.dragTo(q.x, q.y);              // back to the SECOND tile
  if (T.getDrag().length !== 2) fail('truncate to 2nd tile: len ' + T.getDrag().length + ', want 2');
  T.setDrag(chain.slice());
  q = xy(chain[chain.length - 3]); T.dragTo(q.x, q.y);   // back 2 steps (old code popped 1)
  if (T.getDrag().length !== chain.length - 2)
    fail('truncate 2 back: len ' + T.getDrag().length + ', want ' + (chain.length - 2));
  T.setDrag(chain.slice());
  q = xy(chain[chain.length - 1]); T.dragTo(q.x, q.y);   // the last tile: no change
  if (T.getDrag().length !== chain.length) fail('drag on last tile changed the chain');
  T.setDrag(null);
  console.log('A23 truncation ok: 5-tile chain -> 1 / 2 / n-2 / unchanged');

  // ---- 1. game over kills the save (CheckEndGame @0x2f960:156-157 + SurfaceView.java:385-389)
  T.newGame(0xA23, 1, 3, 0);
  T.saveGame();
  if (!localStorage.getItem(LS_KEY)) fail('live run: saveGame must store the save');
  function resolveMenu() {
    const kind = api.menu();
    if (kind < 0) return false;
    const rows = [];
    for (let i = 0; i < 4; i++) { if (api.offer(i, 0) < 0) break; rows.push(i); }
    if (!rows.length) fail('menu ' + kind + ' has no offers');
    if (kind === 0) {
      const sk = rows.filter(i => api.offer(i, 0) === 2);
      const a = sk.length ? sk[0] : rows[0];
      const rest = rows.filter(i => i !== a);
      api.choose(a, rest.length ? (rest.find(i => api.offer(i, 0) === 2) ?? rest[0]) : -1);
    } else api.choose(rows[0], -1);
    while (api.poll() >= 0) {}
    return true;
  }
  function playMove() {
    for (let t = 0; t < 40; t++) {
      const p = findChain(3);
      if (!p) continue;
      const bytes = new Uint8Array(p.map(x => x.c + x.r * 6));
      const ptr = api.buf(bytes.length);
      HEAP().set(bytes, ptr);
      if (api.play(ptr, bytes.length)) return p;
    }
    return null;
  }
  let over = false;
  for (let n = 0; n < 4000 && !over; n++) {
    if (api.menu() >= 0) { resolveMenu(); continue; }
    if (api.over()) { over = true; break; }
    T.saveGame();
    if (!localStorage.getItem(LS_KEY)) fail('mid-run move ' + n + ': live save vanished');
    if (!playMove()) break;
  }
  if (!over) fail('run never died in 4000 moves');
  if (api.save() !== 0 || api.saveLen() !== 0)
    fail('dead game: dr_save must report size 0 (CheckEndGame @0x2f960:156-157), got ' + api.saveLen());
  T.saveGame();
  if (localStorage.getItem(LS_KEY) !== null)
    fail('dead game: saveGame must DELETE the stored save (SurfaceView.java:386-388)');
  T.toMenu();
  if (!document.getElementById('mmContinue').disabled)
    fail('Continue must be disabled after death (D13 7.4)');
  console.log('A23 save-kill ok: dr_save 0 after death, localStorage save deleted, Continue disabled');

  // ---- 3. the web layer never puts a repeated tile into the chain (IsValidPrefix rejects;
  // the original truncates first — ProcessTileSelection @0x2c8dc:163-213)
  T.newGame(0xA23, 1, 3, 0);
  const c3 = findChain(3);
  T.setDrag(c3.slice());
  const a = xy(c3[0]), b = xy(c3[1]);
  T.dragTo(b.x, b.y); T.dragTo(a.x, a.y); T.dragTo(b.x, b.y); T.dragTo(a.x, a.y);
  const d = T.getDrag(), seen = new Set(d.map(x => x.c + x.r * 6));
  if (seen.size !== d.length) fail('drag oscillation produced a repeated tile');
  T.setDrag(null);
  console.log('A23 chain-repeat ok: oscillating drag leaves no repeats (len ' + d.length + ')');

  console.log('PASS: A23 truncation + save-kill + no-repeat');
})().catch(e => fail(e.stack));
