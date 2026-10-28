// U9 node test (TEST HARNESS code only — not game logic): proves the two new storage guards
// in template.html against a REAL dr.js module:
//  1. boot with a CORRUPT/foreign meta blob -> dr_meta_load rejects it -> the stored bytes are
//     PRESERVED under 'tileRaidRebuildMetaV1.bak' (never silently wiped) + mmNotice shown.
//  2. loadGame() with a save the engine rejects (dr_load 0) -> ONLY the in-run key
//     (tileRaidRebuildSaveV1) is dropped + mmNotice shown; meta/settings keys untouched.
// Usage: node test_u9_persist.js   (in rebuild/web)
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
  addEventListener() {}, currentScript: { src: 'x' },
  fonts: { add() {} },
  getElementById(id) {
    if (!byId.has(id)) byId.set(id, (id === 'board' || id === 'fxLayer' || id === 'dimLayer' || id === 'hudBar')
      ? Object.assign(el('canvas'), { getContext: () => ctxStub }) : el('div'));
    return byId.get(id);
  },
  createElement: el,
};
// seeded storage: a FOREIGN meta blob (magic/length check fails) + a save the engine rejects
// (byte 0 != 0xff marker -> dr_load 0), plus normal settings keys that must survive.
const LS_M = new Map();
LS_M.set('tileRaidRebuildMetaV1', Buffer.from('NOT-A-DRM1-BLOB').toString('base64'));
LS_M.set('tileRaidRebuildSaveV1', Buffer.from([0x41, 0x42, 0x43, 0x44]).toString('base64')); // no 0xff
LS_M.set('tileRaidRebuildSound', '0');
LS_M.set('tileRaidRebuildClock', '1');
const localStorage = {
  m: LS_M,
  getItem(k) { return this.m.has(k) ? this.m.get(k) : null; },
  setItem(k, v) { this.m.set(k, v); },
  removeItem(k) { this.m.delete(k); },
};
const window = { addEventListener() {}, devicePixelRatio: 1 };
const performance = { now: () => 0 };

const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
const fxMod = { exports: {} };
new Function('module', blocks.find(b => b.includes('A4a animation physics')))(fxMod);
const app = blocks[blocks.length - 1].replace(/__ART__/g, 'false').replace(/__RECREATED__/g, 'null');
const epilogue = `
;globalThis.__T = { loadGame, hasSave, mmNotice,
  noticeState: () => { const e = document.getElementById('mmNotice');
    return { hidden: e.hidden, text: e.textContent }; } };`;

(async () => {
  const factory = require(path.join(__dirname, 'dr.js'));
  const DR = () => factory();
  const run = new Function('window', 'document', 'localStorage', 'performance', 'DR',
    'fetch', 'Audio', 'FontFace', 'FX', app + epilogue);
  run(window, document, localStorage, performance, DR,
    () => Promise.reject(new Error('no fetch in test')), function () {}, function () {}, fxMod.exports);
  await new Promise(r => setTimeout(r, 250));
  const T = globalThis.__T;
  if (!T) fail('epilogue did not run (app script threw in boot?)');

  // ---- 1. boot meta guard: corrupt blob kept as .bak, notice shown, main key still there ----
  const bak = localStorage.getItem('tileRaidRebuildMetaV1.bak');
  if (bak !== LS_M.get('tileRaidRebuildMetaV1') && bak !== Buffer.from('NOT-A-DRM1-BLOB').toString('base64'))
    fail('boot did not preserve the unreadable meta blob under .bak (got: ' + bak + ')');
  if (localStorage.getItem('tileRaidRebuildMetaV1') === null)
    fail('boot REMOVED the original meta key — must never silently wipe');
  const n1 = T.noticeState();
  if (n1.hidden || !n1.text) fail('boot meta failure did not raise the mmNotice line');
  console.log('U9 boot guard ok: meta .bak preserved + notice shown');

  // ---- 2. loadGame guard: engine-rejected save dropped, everything else untouched ----------
  const ok = T.loadGame();
  if (ok !== false) fail('loadGame must return false for a rejected save');
  if (localStorage.getItem('tileRaidRebuildSaveV1') !== null)
    fail('engine-rejected run save was not dropped (task: drop only the in-run save)');
  for (const k of ['tileRaidRebuildMetaV1', 'tileRaidRebuildMetaV1.bak',
                   'tileRaidRebuildSound', 'tileRaidRebuildClock']) {
    if (localStorage.getItem(k) === null) fail('loadGame touched non-save key ' + k);
  }
  const n2 = T.noticeState();
  if (n2.hidden || !n2.text) fail('loadGame drop did not raise the mmNotice line');
  console.log('U9 loadGame guard ok: only SaveV1 dropped, meta/settings intact, notice shown');
  console.log('PASS: U9 storage guards (never silently wipe meta; drop only the run save)');
  process.exit(0);
})().catch(e => { console.error('ERR', e); process.exit(2); });
