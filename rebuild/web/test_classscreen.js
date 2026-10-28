// W4b node test (W5: real dr.js, no fake-W3 mode): run template.html's app script in a
// minimal DOM shim against the REAL wasm module and exercise the ClassScreen (4 tabs x 10
// classes), classSet accept/refuse paths, and the unlock pop-up drain. Class levels are
// unlocked by patching the dr_meta_save blob (same technique as test_headless.js).
// Test harness code only — not game logic.
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
  setItem(k, v) { this.m.set(k, v); } };
const window = { addEventListener() {}, devicePixelRatio: 1 };
const performance = { now: () => 0 };

const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
const fxMod = { exports: {} };   // A4a: the app script uses the pure FX physics block
new Function('module', blocks.find(b => b.includes('A4a animation physics')))(fxMod);
const app = blocks[blocks.length - 1].replace(/__ART__/g, 'false').replace(/__RECREATED__/g, 'null');
const epilogue = `
;globalThis.__T = {
  openClassScreen, renderClassScreen, drainUnlocks, commitClassScreen,
  setSel: (k, t) => { clsSel = k; clsTab = t; skSel = -1; clsMsg = ''; },
  api: () => api, M: () => M,
  classScreenGen: (t, s, r, l, u, v, p, f, rp, pl) => classScreenGen(t, s, r, l, u, v, p, f, rp, pl),
  ORIG_DATA: () => ORIG_DATA,
};`;

(async () => {
  const factory = require(path.join(__dirname, 'dr.js'));   // REAL module (W5: no fake-W3 mode)
  const DR = () => factory();
  const run = new Function('window', 'document', 'localStorage', 'performance', 'DR',
    'fetch', 'Audio', 'FontFace', 'FX', app + epilogue);
  run(window, document, localStorage, performance, DR,
    () => Promise.reject(new Error('no fetch in test')), function () {}, function () {}, fxMod.exports);
  await new Promise(r => setTimeout(r, 150));   // let boot() settle (wasm instantiate)
  const T = globalThis.__T;
  if (!T) fail('epilogue did not run (app script threw in boot?)');
  const api = T.api();

  // dr_meta_save needs a live game (classes blob); meta container: 4 magic + 4 len + 472
  // (2 B header + 10 x 47 B classes, level byte at record+26) + scores (see test_headless.js).
  api.newGame(1, 1, 3, 0);
  const HEAP = () => T.M().HEAPU8;
  const setLevels = levels => {
    const mp = api.metaSave(), ml = api.metaSaveLen();
    if (!mp || ml < 472) fail('dr_meta_save produced ' + ml + ' bytes');
    const meta = HEAP().slice(mp, mp + ml);
    const off = 8 + 2, REC = 47, LVL = 26;
    for (const [k, lvl] of Object.entries(levels)) meta[off + k * REC + LVL] = lvl;
    const lp = api.buf(ml);
    HEAP().set(meta, lp);
    if (!api.metaLoad(lp, ml)) fail('dr_meta_load rejected the patched blob');
  };

  // all 4 tabs x 10 classes, fresh meta state (only Adventurer unlocked)
  T.openClassScreen();
  for (let k = 0; k < 10; k++)
    for (let t = 0; t < 4; t++) { T.setSel(k, t); T.renderClassScreen(); }

  // level everything up: gates open (race at 5, slots at 3/7/9, replacements at 2/4/6/8/10)
  const all10 = {}; for (let k = 0; k < 10; k++) all10[k] = 10;
  setLevels(all10);
  if (api.cls(1, 0) !== 10) fail('meta patch did not set class 1 to level 10');
  for (let k = 0; k < 10; k++)
    for (let t = 0; t < 4; t++) { T.setSel(k, t); T.renderClassScreen(); }

  // engine-side accept/refuse through dr_class_set (rules cited in src/m8.cpp)
  T.setSel(1, 2);
  if (api.classSet(1, 0, 0, 3) !== 0) fail('SetPerk accepted slot 0 (SetPerk @0x35e18 slot>0)');
  if (api.classSet(1, 0, 1, 6) !== 0) fail('SetPerk accepted duplicate of inherent Agile');
  if (api.classSet(1, 0, 1, 0) !== 1) fail('SetPerk refused Unexpected in open slot 1');
  if (api.classSet(1, 0, 2, 0) !== 0) fail('SetPerk accepted duplicate Unexpected');
  if (api.classSet(1, 1, 1, 0) !== 1) fail('SetFlaw refused Confused in open slot 1');
  if (api.classSet(1, 2, 0, 5) !== 1) fail('race change refused (Orc unlocked at lv10)');
  if (api.classSet(1, 2, 0, 0) !== 1) fail('race change to Human refused');
  setLevels({ 5: 1 });
  if (api.classSet(1, 2, 0, 9) !== 0) fail('race change accepted (Changeling owner Mage lv1)');
  setLevels({ 5: 10 });
  if (api.classSet(1, 3, 0, 0) !== 0) fail('replacement accepted for class skill slot (SetReplacementSkill @0x364dc slot>0)');
  if (api.classSet(1, 3, 1, 0) !== 1) fail('replacement refused in open slot');
  if (api.classSet(1, 3, 2, 0) !== 1) fail('replacement overwrite refused');
  setLevels({ 1: 1 });
  if (api.classSet(1, 3, 3, 0) !== 0) fail('replacement accepted over cap (allowed 0 at lv1)');
  if (api.classSet(1, 0, 1, 0) !== 0) fail('perk accepted in locked slot at lv1');

  // class stat rows (dr_class 54-58) render as numbers, not '—'
  setLevels({ 1: 10 });
  T.setSel(1, 0); T.renderClassScreen();
  const kv = document.getElementById('clsBody').children.find(c => c.className === 'kv');
  if (!kv) fail('tab 0: no .kv stat block');
  const vals = kv.children.filter(c => c.className === 'v').map(c => c.textContent);
  if (vals.length !== 5 || vals.some(v => v === '—' || v === '')) fail('tab 0: stat rows not filled: ' + vals);

  // click-through: perk card onclick places via classSet (find a perk card in tab 2 body)
  T.setSel(1, 2); T.renderClassScreen();
  const body = document.getElementById('clsBody');
  const grids = body.children.filter(c => c.children.length === 10);
  if (grids.length < 2) fail('tab 2: expected 2 10-card grids, got ' + grids.length);
  const perkGrid = grids[0];
  const card = perkGrid.children.find(c => c.onclick);
  if (!card) fail('tab 2: no clickable perk card');
  card.onclick();

  // skills tab: click slot 1 then a pool card
  T.setSel(1, 3); T.renderClassScreen();
  const skGrids = document.getElementById('clsBody').children.filter(c => c.children.length === 20);
  if (!skGrids.length) fail('tab 3: no 20-card skill grid');
  skGrids[0].children[1].onclick();
  const pool = document.getElementById('clsBody').children.filter(c => c.children.length >= 30);
  if (pool.length === 1) pool[0].children[0].onclick();
  else fail('tab 3: pool grid not found (got ' + pool.length + ')');

  // unlock pop-ups: fake a queue (test-harness monkey-patch of api.unlockNext)
  let n = 3;
  api.unlockNext = (field) => (field === 0 && n-- > 0) ? 1 : -1;
  T.drainUnlocks();
  const ov = document.getElementById('unlockOverlay');
  if (ov.hidden !== false) fail('popup overlay not shown');
  const title = document.getElementById('ukTitle').textContent;
  if (title !== 'Ranger level up!') fail('popup title: ' + title);
  document.getElementById('ukOk').onclick();
  document.getElementById('ukOk').onclick();
  document.getElementById('ukOk').onclick();
  if (ov.hidden !== true) fail('popup overlay not closed after 3 OKs');

  // commit (FadeOut): locked class -> Adventurer
  setLevels({ 1: 0 });
  T.setSel(1, 0); T.commitClassScreen();
  if (byId.get('scrMenu').classList.contains('on') !== true) fail('commit did not return to menu');

  // A26q (HARNESS): classScreenGen() — the JS port of the state-driven part of
  // ClassScreen::Render @00037f5c (tab strip/divider/panel/tab icons/class grid/race grid/
  // level row, cited constants) — must reproduce the captured state-driven quads
  // (ORIG_DATA.clsGen, generated by tools/assets/orig_quads.py from the emu captures) for the
  // 5 captured states, +-1 px (capture float32 ortho rounding).
  const OD = T.ORIG_DATA();
  const near = (a, b) => a[0] === b[0] && Math.abs(a[1] - b[1]) <= 1 && Math.abs(a[2] - b[2]) <= 1
    && Math.abs(a[3] - b[3]) <= 1 && Math.abs(a[4] - b[4]) <= 1 &&
    a.slice(5).every((x, i) => x === b[5 + i]);
  const lvls = () => { const u = []; for (let c = 0; c < 10; c++) u.push(api.cls(c, 0)); return u; };
  const races = () => { const v = []; for (let r = 0; r < 10; r++) { let u = 0;
    for (let c = 0; c < 10; c++) if (api.cls(c, 1) === r && api.cls(c, 0) >= 1) u = 1; v.push(u); } return v; };
  const pf = (k) => { const p = [], f = [], rp = [], pl = [];
    for (let s = 0; s < 4; s++) { p.push(api.cls(k, 3 + s)); f.push(api.cls(k, 7 + s)); }
    for (let s = 0; s < 20; s++) rp.push(api.cls(k, 34 + s));
    for (let s = 0; s < 22; s++) pl.push(api.cls(k, 59 + s));
    return [p, f, rp, pl]; };
  const chk = (name, tab, sel) => {
    const st = pf(sel);
    const gg = T.classScreenGen(tab, sel, api.cls(sel, 1), api.cls(sel, 0), lvls(), races(),
                                st[0], st[1], st[2], st[3]);
    if (gg.length !== OD.clsGen[name].length ||
        !gg.every((r, i) => near(r, OD.clsGen[name][i]))) fail('A26q gen != capture ' + name);
  };
  setLevels({ 0: 1, 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0, 7: 0, 8: 0, 9: 0 });
  chk('class_t0', 0, 0);
  setLevels({ 0: 1, 1: 1, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0, 7: 0, 8: 0, 9: 0 });
  chk('class_t0_c1', 0, 1);
  setLevels({ 0: 10, 1: 10, 2: 10, 5: 10, 3: 0, 4: 0, 6: 0, 7: 0, 8: 0, 9: 0 });
  for (const kk of [['class_t0_c5', 0], ['class_t1_c5', 1], ['class_t2_c5', 2], ['class_t3_c5', 3]])
    chk(kk[0], kk[1], 5);

  console.log('PASS: class screen 4 tabs x 10 classes, set/refuse rules, pop-up queue, commit (real dr.js)');
})().catch(e => fail(e && e.stack || String(e)));
