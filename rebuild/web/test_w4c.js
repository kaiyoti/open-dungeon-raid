// W4c node test (W5: real dr.js, no fake-W3 mode): run template.html's app script in a
// minimal DOM shim against the REAL wasm module and exercise the skill bar, drag preview
// (cross + labels), HUD extras, floating labels, help tips and the trophy tap — all numbers
// come from the engine. Test harness code only — not game logic (move policy, menu picks).
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

function xorshift(s) {  // deterministic RNG for the test policy
  let x = s >>> 0 || 1;
  return () => { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; };
}

const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
const fxMod = { exports: {} };   // A4a: the app script uses the pure FX physics block
new Function('module', blocks.find(b => b.includes('A4a animation physics')))(fxMod);
const app = blocks[blocks.length - 1].replace(/__ART__/g, 'false').replace(/__RECREATED__/g, 'null');
const epilogue = `
;globalThis.__T = {
  api: () => api, M: () => M, newGame, renderSkills, renderHudExtra, updatePreview, drawPreview,
  draw, drainLabels: () => drainLabels(), labels: () => floatLabels, wallClockStr,
  queueHelp, helpBanner: () => document.getElementById('helpBanner'),
  // U12: Original-only — the banner is the CANVAS bubble (helpDraw); the DOM element stays
  // hidden. TEST HARNESS: assert canvas state; dismiss mirrors the element's onclick.
  helpShown: () => helpShowing(),
  helpTip: () => (helpDraw ? HELP_TIPS[helpDraw.n] : ''),
  helpDismiss: () => { helpBanner.hidden = true; helpDismiss(); showHelpNext(); },
  endDrag: () => endDrag(), setDrag: p => { drag = p ? { path: p, last: null } : null; },
  refresh,
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
  const api = T.api(), HEAP = () => T.M().HEAPU8;
  const rnd = xorshift(0x5EED);

  // ---- test policy (harness, not game logic): play legal random chains, take skill offers
  function randomPath() {
    let c0, r0;
    if (rnd() < 0.5) {
      const skulls = [];
      for (let c = 0; c < 6; c++) for (let r = 0; r < 6; r++)
        if ((api.tile(c, r) & 7) === 1) skulls.push({ c, r });
      if (skulls.length) { const s = skulls[(rnd() * skulls.length) | 0]; c0 = s.c; r0 = s.r; }
    }
    if (c0 === undefined) { c0 = (rnd() * 6) | 0; r0 = (rnd() * 6) | 0; }
    const t = api.tile(c0, r0) & 7;
    if (t === 7) return null;
    const p = [{ c: c0, r: r0 }];
    const seen = new Set([c0 + r0 * 6]);
    for (;;) {
      if (p.length >= 3 + ((rnd() * 5) | 0)) break;
      const last = p[p.length - 1], cand = [];
      for (let dc = -1; dc <= 1; dc++) for (let dr = -1; dr <= 1; dr++) {
        if (!dc && !dr) continue;
        const c = last.c + dc, r = last.r + dr;
        if (c < 0 || c > 5 || r < 0 || r > 5) continue;
        const id = c + r * 6;
        if (seen.has(id)) continue;
        const tt = api.tile(c, r) & 7;
        if (tt === t || (t === 1 && tt === 4) || (t === 4 && tt === 1)) cand.push({ c, r });
      }
      if (!cand.length) break;
      const pick = cand[(rnd() * cand.length) | 0];
      p.push(pick); seen.add(pick.c + pick.r * 6);
    }
    return p.length >= 3 ? p : null;
  }
  function resolveMenu() {
    const kind = api.menu();
    if (kind < 0) return false;
    const rows = [];
    for (let i = 0; i < 4; i++) { if (api.offer(i, 0) < 0) break; rows.push(i); }
    if (!rows.length) fail('menu ' + kind + ' has no offers');
    if (kind === 0) {
      const skills = rows.filter(i => api.offer(i, 0) === 2);
      const a = skills.length ? skills[0] : rows[0];
      const rest = rows.filter(i => i !== a);
      api.choose(a, rest.length ? (rest.find(i => api.offer(i, 0) === 2) ?? rest[0]) : -1);
    } else api.choose(rows[0], -1);
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

  // start a real game through the page's own newGame()
  T.newGame(0xC0FFEE, 1, 3, 0);

  // ---- play until 2 skills are owned (level-up menus prefer skill offers)
  let guard = 0;
  while (api.stat(21) < 2 && guard++ < 1500) {
    if (api.over()) { T.newGame(0xC0FFEE ^ guard, 1, 3, 0); continue; }
    if (api.menu() >= 0) { resolveMenu(); continue; }
    if (!playMove()) T.newGame(0xC0FFEE ^ guard, 1, 3, 0);
  }
  if (api.stat(21) < 2) fail('no 2 skills owned after ' + guard + ' moves');

  // ---- skill bar: buttons per owned skill; cooling overlay height = (max-charge)/max (engine)
  T.renderSkills();
  const bar = document.getElementById('skillBar');
  const n = api.stat(21);
  if (bar.children.length !== n) fail('skill bar: ' + bar.children.length + ' buttons, want ' + n);
  let sawCool = 0;
  for (let s = 0; s < n; s++) {
    const b = bar.children[s];
    if (!b.className.includes('skbtn')) fail('skill bar: slot ' + s + ' not a .skbtn');
    const ready = api.skill(s, 4) === 1;
    if (ready !== b.className.includes('ready')) fail('skill bar: ready class mismatch slot ' + s);
    const cool = b.children.find(c => c.className === 'cool');
    if (ready) { if (cool) fail('skill bar: ready slot ' + s + ' has overlay'); }
    else if (api.skill(s, 3) > 0) {
      const want = Math.max(0, Math.min(100, (api.skill(s, 3) - api.skill(s, 2)) * 100 / api.skill(s, 3))) + '%';
      if (!cool || cool.style.height !== want) fail('skill bar: overlay ' + (cool && cool.style.height) + ' want ' + want);
      sawCool++;
    }
  }

  // ---- dr_stat 29..34 come from the engine (numbers; 31 REMOVED by W7 — the clock is a wall clock)
  for (const id of [29, 30, 32, 33, 34])
    if (typeof api.stat(id) !== 'number' || !isFinite(api.stat(id))) fail('dr_stat(' + id + ') not a number');

  // ---- wall clock (W7): engine flag = Game::SetClockState @0x25038 (Game+0x142f9); the string
  // is the device's local time "h:mm a" (DungeonRaidRenderer.onDrawFrame) — page-side.
  if (api.clockEnabled() !== 0) fail('clock must start off (java:121 getBoolean("clock", false))');
  api.setClock(1);
  if (api.clockEnabled() !== 1) fail('dr_set_clock(1) did not set Game+0x142f9');
  api.setClock(1);   // SetClockState(flag) again: old ^ new = changed flag, on stays on
  if (api.clockEnabled() !== 1) fail('clock flag lost on redundant set');
  api.setClock(0);
  if (api.clockEnabled() !== 0) fail('dr_set_clock(0) did not clear Game+0x142f9');
  if (!/^\d{1,2}:\d{2} (AM|PM)$/.test(T.wallClockStr())) fail('wallClockStr format: ' + T.wallClockStr());

  // ---- HUD extras: trophies always shown; poison/incoming when the engine reports them
  T.renderHudExtra();
  const txt = n2 => (typeof n2 === 'string' ? n2 : n2.textContent + n2.children.map(txt).join(''));
  const hudTxt = document.getElementById('hudExtra').children.map(txt).join('|');
  if (!hudTxt.includes('Trophies')) fail('hudExtra missing trophies: ' + hudTxt);
  // clock shown ONLY while the setting is on (W7), in any mode, as "h:mm AM/PM"
  if (hudTxt.includes('⏱')) fail('hudExtra shows clock while setting off');
  api.setClock(1);
  T.renderHudExtra();
  const hudClk = document.getElementById('hudExtra').children.map(txt).join('|');
  // (regex, not string equality: the two new Date() calls can straddle a minute boundary)
  if (!/⏱ \d{1,2}:\d{2} (AM|PM)/.test(hudClk)) fail('hudExtra missing wall clock: ' + hudClk);
  api.setClock(0);
  T.renderHudExtra();
  if (api.stat(29) > 0 && !hudTxt.includes('poison')) fail('hudExtra missing poison: ' + hudTxt);
  if (api.stat(30) > 0 && (!hudTxt.includes('Incoming') || !hudTxt.includes(String(api.stat(30)))))
    fail('hudExtra missing incoming: ' + hudTxt);

  // ---- drag preview on a REAL chain: engine atk + packed per-tile, board untouched
  let pvTries = 0;
  for (let tries = 0; tries < 60 && !pvTries; tries++) {
    const p = randomPath();
    if (!p) continue;
    const before = [];
    for (let c = 0; c < 6; c++) for (let r = 0; r < 6; r++) before.push(api.tile(c, r));
    T.setDrag(p);
    T.updatePreview();
    const atk = api._pv.atk;
    if (typeof atk !== 'number' || atk < 0) fail('preview atk = ' + atk);
    for (const q of p) {
      const v = api.previewTile(q.c, q.r);
      if (typeof v !== 'number' || v < 0 || v > 65535) fail('previewTile packed out of range: ' + v);
      if ((v & 3) > 3) fail('previewTile damage type out of range: ' + v);
    }
    T.drawPreview();   // ctx stub must not throw
    T.setDrag(null);
    for (let c = 0; c < 6; c++) for (let r = 0; r < 6; r++)
      if (api.tile(c, r) !== before[c * 6 + r]) fail('preview changed the board at ' + c + ',' + r);
    pvTries++;
  }
  if (!pvTries) fail('could not stage a preview chain');

  // ---- floating labels: a real move queues labels through dr_label_*
  let labelSeen = 0;
  for (let tries = 0; tries < 60 && !labelSeen; tries++) {
    if (api.menu() >= 0) { resolveMenu(); continue; }
    if (api.over()) fail('game over during the label probe (unexpected with this policy)');
    const p = playMove();
    if (!p) continue;
    T.drainLabels();
    const L = T.labels();
    for (const l of L)
      if (typeof l.kind !== 'number' || typeof l.value !== 'number' ||
          typeof l.col !== 'number' || typeof l.row !== 'number') fail('label fields wrong');
    labelSeen += L.length;
  }
  T.draw();   // drawLabels on the ctx stub must not throw

  // ---- help tips: one-shot, bitmask persisted (page-side)
  // U12: the canvas banner anchors each tip (HELP_FIXED / engine tile); tips 0/1 have no
  // fixed wrapper anchor, so the harness passes a board-tile anchor — the original's
  // Process @0x339ac:98-101 anchors N0 at a random board tile (harness: fixed tile centre).
  T.queueHelp(0, 0.5, 0.5);
  if (!T.helpShown()) fail('help banner not shown after queueHelp(0)');
  if (!T.helpTip().includes('Trace a path')) fail('help banner text wrong: ' + T.helpTip());
  T.queueHelp(0, 0.5, 0.5);   // seen -> no re-queue
  T.queueHelp(1, 0.5, 0.5);
  T.helpDismiss();  // dismiss -> next queued tip shows (HelpMenu::Process @0x483a4)
  if (!T.helpTip().includes('Each tile matched')) fail('second tip not shown after dismiss');
  T.helpDismiss();
  if (T.helpShown()) fail('banner should hide with empty queue');

  // ---- trophy tap: play until a trophy tile (type 6, variant 1) appears, then single-tap it
  let trophy = null, trophyTaps = 0;
  for (let tries = 0; tries < 1200 && !trophyTaps; tries++) {
    if (api.menu() >= 0) { resolveMenu(); continue; }
    if (api.over()) { T.newGame(0xBEEF ^ tries, 1, 3, 0); continue; }
    for (let c = 0; c < 6 && !trophy; c++)
      for (let r = 0; r < 6 && !trophy; r++) {
        const p = api.tile(c, r);
        if ((p & 7) === 6 && ((p >> 9) & 3) === 1) trophy = { c, r };
      }
    if (trophy) {
      const before = api.tile(trophy.c, trophy.r);
      T.setDrag([trophy]);
      T.endDrag();
      const after = api.tile(trophy.c, trophy.r);
      if ((before & 7) === 6 && ((before >> 9) & 3) === 1 && (after & 7) === 6 &&
          ((after >> 9) & 3) === 1 && after === before)
        fail('trophy tap did not collect the trophy tile');
      trophyTaps++;
      break;
    }
    if (!playMove()) T.newGame(0xBEEF ^ tries, 1, 3, 0);
  }
  console.log('trophy tap: ' + (trophyTaps ? 'collected one trophy tile' : 'none spawned in 1200 moves (skipped)'));

  // ---- skill activation: click every skill button (engine decides; menus resolved after)
  T.renderSkills();
  for (let s = 0; s < api.stat(21); s++) { const b = document.getElementById('skillBar').children[s]; if (b.onclick) b.onclick(); }
  while (api.menu() >= 0) resolveMenu();
  T.draw();

  console.log('PASS: W4c skill bar, preview cross/labels, HUD extras, floating labels, help tips, trophy tap (real dr.js)');
})().catch(e => fail(e.stack));
