// G3 node test (TEST HARNESS code only — not game logic): scNewRank persistence.
// Play a classic run to death -> dr_score_add -> scNewRank set -> persistMeta (the page's
// scores save) -> RELOAD (fresh page instance + fresh wasm, same localStorage) -> the
// highlight row is the same. Then the reset-button clear: scores + highlight gone.
// Cites: SerialiseState @0x5cb34:21-27 / DeserialiseState @0x5ca10:25-29 (rank char travels
// with the scores state), AddScore @0x5cc5c:26-28 (new score overwrites), ctor @0x5e6f8:62-63.
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
  addEventListener() {},
  currentScript: { src: 'x' },
  fonts: { add() {} },
  getElementById(id) {
    if (!byId.has(id)) byId.set(id, (id === 'board' || id === 'fxLayer' || id === 'dimLayer' || id === 'hudBar') ? Object.assign(el('canvas'), { getContext: () => ctxStub }) : el('div'));
    return byId.get(id);
  },
  createElement: el,
};
function makeLS() {
  return { m: new Map(), getItem(k) { return this.m.has(k) ? this.m.get(k) : null; },
    setItem(k, v) { this.m.set(k, String(v)); }, removeItem(k) { this.m.delete(k); } };
}
const window = { addEventListener() {}, devicePixelRatio: 1 };
const performance = { now: () => 0 };
function xorshift(s) {
  let x = s >>> 0 || 1;
  return () => { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; };
}

const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
const fxMod = { exports: {} };
new Function('module', blocks.find(b => b.includes('A4a animation physics')))(fxMod);
const app = blocks[blocks.length - 1].replace(/__ART__/g, 'false').replace('__RECREATED__', 'null');
const epilogue = `
;globalThis.__T = {
  api: () => api, M: () => M, newGame, checkOver, persistMeta,
  getScNewRank: () => scNewRank,
  getYell: p => (scNewRank && scNewRank.p === p ? scNewRank.r : -1),   // mirrors the yell expr in drawScoreScreen (F5)
};`;

async function bootInstance(LS) {
  const factory = require(path.join(__dirname, 'dr.js'));
  const run = new Function('window', 'document', 'localStorage', 'performance', 'DR',
    'fetch', 'Audio', 'FontFace', 'FX', app + epilogue);
  run(window, document, LS, performance, () => factory(),
    () => Promise.reject(new Error('no fetch in test')), function () {}, function () {}, fxMod.exports);
  await new Promise(r => setTimeout(r, 150));
  const T = globalThis.__T; globalThis.__T = null;
  if (!T) fail('epilogue did not run (app script threw in boot?)');
  return T;
}

// test policy (harness, as in test_w5_playthrough): random legal chains, menus pick first row
function makePolicy(T, rnd) {
  const api = T.api(), HEAP = () => T.M().HEAPU8;
  function randomPath() {
    const c0 = (rnd() * 6) | 0, r0 = (rnd() * 6) | 0;
    const t = api.tile(c0, r0) & 7;
    if (t === 7) return null;
    const p = [{ c: c0, r: r0 }], seen = new Set([c0 + r0 * 6]);
    for (;;) {
      if (p.length >= 3 + ((rnd() * 5) | 0)) break;
      const last = p[p.length - 1], cand = [];
      for (let dc = -1; dc <= 1; dc++) for (let dr = -1; dr <= 1; dr++) {
        if (!dc && !dr) continue;
        const c = last.c + dc, r = last.r + dr;
        if (c < 0 || c > 5 || r < 0 || r > 5 || seen.has(c + r * 6)) continue;
        const tt = api.tile(c, r) & 7;
        if (tt === t || (t === 1 && tt === 4) || (t === 4 && tt === 1)) cand.push({ c, r });
      }
      if (!cand.length) break;
      const q = cand[(rnd() * cand.length) | 0];
      p.push(q); seen.add(q.c + q.r * 6);
    }
    return p.length >= 3 ? p : null;
  }
  function resolveMenu() {
    const kind = api.menu();
    if (kind < 0) return false;
    const rows = [];
    for (let i = 0; i < 4; i++) { if (api.offer(i, 0) < 0) break; rows.push(i); }
    if (rows.length) api.choose(rows[0], -1);
    while (api.poll() >= 0) {}
    return true;
  }
  function playMove() {
    for (let t = 0; t < 40; t++) {
      const p = randomPath();
      if (!p) continue;
      const bytes = new Uint8Array(p.map(q => q.c + q.r * 6));
      const ptr = api.buf(bytes.length);
      HEAP().set(bytes, ptr);
      if (api.play(ptr, bytes.length)) return p;
    }
    return null;
  }
  function playToDeath() {
    for (let n = 0; n < 4000; n++) {
      if (api.menu() >= 0) { resolveMenu(); continue; }
      if (api.over()) return true;
      if (!playMove()) break;
    }
    return api.over();
  }
  return { playToDeath };
}

(async () => {
  // ---- instance 1: run to death, score insert, save ------------------------------------
  const LS = makeLS();
  const T1 = await bootInstance(LS);
  const P1 = makePolicy(T1, xorshift(0x51A7));
  T1.newGame(0xF00D, 1, 3, 0);
  if (!P1.playToDeath()) fail('classic run never ended in 4000 moves');
  T1.checkOver();   // page: overlay + dr_score_add insert (once) -> scNewRank
  const api1 = T1.api();
  const snr1 = T1.getScNewRank();
  if (!snr1) fail('scNewRank not set after game over (rank -1 on a fresh profile?)');
  const page = snr1.p, rank = snr1.r;
  if (page !== api1.difficulty()) fail('scNewRank page ' + page + ' != ScoreType ' + api1.difficulty());
  const insScore = api1.score(page, rank, 0);
  if (insScore < 0) fail('scNewRank row (' + page + ',' + rank + ') has no entry');
  T1.persistMeta();   // the page's existing scores save
  const KEY = 'tileRaidRebuildScNewRankV1';
  if (LS.getItem(KEY) !== page + ':' + rank)
    fail('scores save did not store the highlight: ' + LS.getItem(KEY) + ' want ' + page + ':' + rank);
  if (!LS.getItem('tileRaidRebuildMetaV1')) fail('meta blob missing after persistMeta');
  console.log('save ok: highlight ' + page + ':' + rank + ' (score ' + insScore + ') stored with the meta blob');

  // ---- instance 2: RELOAD (fresh page + fresh wasm, same localStorage) -----------------
  const T2 = await bootInstance(LS);
  const api2 = T2.api();
  const snr2 = T2.getScNewRank();
  if (!snr2) fail('scNewRank not restored after reload');
  if (snr2.p !== page || snr2.r !== rank)
    fail('highlight row changed: ' + snr2.p + ':' + snr2.r + ' want ' + page + ':' + rank);
  if (api2.score(page, rank, 0) !== insScore)
    fail('the highlighted row is not the same entry after reload');
  if (T2.getYell(page) !== rank) fail('score screen would not highlight row ' + rank + ' on page ' + page);
  if (T2.getYell((page + 1) % 6) !== -1) fail('other page shows a highlight');
  console.log('reload ok: row ' + page + ':' + rank + ' highlighted, entry score ' + insScore + ' intact');

  // ---- clear: the reset button drops the meta blob AND the highlight (cite in template) --
  const resetSrc = /reset\.onclick = \(\) => \{[\s\S]*?\};/.exec(html);
  if (!resetSrc || !resetSrc[0].includes("'tileRaidRebuildMetaV1'") || !resetSrc[0].includes('LS_SCNR'))
    fail('reset handler does not remove both the meta blob and LS_SCNR');
  LS.removeItem('tileRaidRebuildMetaV1'); LS.removeItem(KEY);   // what the handler does
  const T3 = await bootInstance(LS);
  if (T3.getScNewRank()) fail('highlight survived the scores clear');
  if (T3.getYell(page) !== -1) fail('cleared profile still highlights page ' + page);
  console.log('clear ok: reset drops the highlight with the scores (ctor @0x5e6f8:62-63 = 0xffffffff)');
  console.log('PASS: G3 scNewRank save -> reload -> same highlight row, + reset clear');
})().catch(e => { console.error('FAIL: ' + (e && e.stack || e)); process.exit(1); });
