// A24 node test: BACK key, skill press/release, settings toggles, help tips — REAL wasm.
// Covers task A24 items:
//   1. BACK/Escape (Game::BackPressed @0x26a98): help bubble first (@:24-25), in-game
//      pause-menu (@:26-29), sub-screens -> main menu (@:33-50), menu-open = no-op;
//   2. skill press/release (Interface::ProcessInput @0x49738): DOWN shows the tooltip
//      (@:50-51) and arms ONLY when ready (@:36-40); UP activates ONLY on the SAME button
//      (CursorInButton @:88) + ready; release outside = nothing; tooltip hides on UP (@:101);
//   3. settings: help master switch (SetHelpState @0x2505c + UpdateHelpState @0x24fb0),
//      attack dimming (Game::Render @0x25924:104-108 gate), smooth (SetHighFramerate
//      @0x24fe8 — A28b: no timing effect, flag only);
//   4. help tips N4 (sword-without-skull, ProcessInput @0x330c0:252) and N14 (low health
//      ratio <= 0.35, ProcessHeartbeat @0x48dfc:26) fire once through the seen bitmask.
// Test harness code only — not game logic (move policy, menu picks, fake-anim probe).
'use strict';
const fs = require('fs');
const path = require('path');

function fail(msg) { console.error('FAIL: ' + msg); process.exit(1); }

function el(tag) {
  const e = {
    tag, children: [], style: {}, hidden: true, textContent: '', _innerHTML: '',
    type: '', className: '', disabled: false, width: 0, height: 0, clientWidth: 480,
    listeners: {},
    classList: {
      s: new Set(),
      add(c) { this.s.add(c); }, remove(c) { this.s.delete(c); },
      contains(c) { return this.s.has(c); },
      toggle(c, on) { on ? this.s.add(c) : this.s.delete(c); },
    },
    append(...kids) { for (const k of kids) e.children.push(k); },
    appendChild(k) { e.children.push(k); return k; },
    addEventListener(t, f) { (e.listeners[t] = e.listeners[t] || []).push(f); },
    setPointerCapture() {},
    contains(x) { return this.children.includes(x); },   // A24: CursorInButton shim
    getBoundingClientRect() { return { left: 0, top: 0, width: 480, height: 480 }; },
    querySelector() { return el('div'); },
    querySelectorAll(sel) { return e.children.filter(c => c.className === sel.slice(1)); },
    onclick: null,
  };
  Object.defineProperty(e, 'innerHTML', {
    get() { return e._innerHTML; },
    set(v) {
      e._innerHTML = v;
      e.children.length = 0;
      if (typeof v === 'string' && v.includes('<')) {
        for (const m of v.matchAll(/class="([^"]+)"/g)) {
          const k = el('div');
          k.className = m[1];
          e.children.push(k);
        }
      }
    },
  });
  e.querySelector = (sel) => e.children.find(c => c.className === sel.slice(1)) || el('div');
  e.querySelectorAll = (sel) => e.children.filter(c => c.className === sel.slice(1));
  return e;
}
const ctxStub = require('./canvas_stub').makeStrictCtx();

const byId = new Map();
const docL = {};
let efp = null;   // elementFromPoint target (A24: CursorInButton @0x49738:88 shim)
const document = {
  addEventListener(t, f) { (docL[t] = docL[t] || []).push(f); },   // A24 BACK keydown
  elementFromPoint() { return efp; },
  currentScript: { src: 'x' },
  fonts: { add() {} },
  getElementById(id) {
    if (!byId.has(id)) byId.set(id, (id === 'board' || id === 'fxLayer' || id === 'dimLayer' || id === 'hudBar') ? Object.assign(el('canvas'), { getContext: () => ctxStub }) : el('div'));
    return byId.get(id);
  },
  createElement(tag) {
    const e = el(tag);
    if (tag === 'canvas') {
      e.getContext = () => ctxStub;
      e.toDataURL = () => 'data:image/png;base64,';
    }
    return e;
  },
};
const localStorage = { m: new Map(), getItem(k) { return this.m.has(k) ? this.m.get(k) : null; },
  setItem(k, v) { this.m.set(k, v); } };
const window = { addEventListener() {}, devicePixelRatio: 1 };
const performance = { now: () => 0 };

const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
const fxMod = { exports: {} };
new Function('module', blocks.find(b => b.includes('A4a animation physics')))(fxMod);
const app = blocks[blocks.length - 1]
  .replace(/__ART__/g, 'true').replace(/__RECREATED__/g, 'null')
  .replace(/__ASSET_BASE__/g, '../../assets/ripped');
const epilogue = `
;globalThis.__T = {
  api: () => api, M: () => M, newGame, draw, toMenu, showScreen,
  curScreen: () => curScreen,
  helpAvail: () => helpAvail,
  setHelpAvail: v => { helpAvail = v >>> 0; api.setHelpState(helpAvail); },
  helpQueue: () => helpQueue, helpBanner: () => helpBanner,
  // F5: the Original theme draws the banner on #fxLayer (helpDraw) and never unhides the
  // DOM element — assert the theme-abstract state instead of the element flag.
  helpShown: () => helpShowing(),
  // U12: Original-only — the banner is the CANVAS bubble (helpDraw); read its tip text from
  // the canvas state. TEST HARNESS: not game logic.
  helpTip: () => (helpDraw ? HELP_TIPS[helpDraw.n] : ''),
  setChainCell: v => { lastChainCell = v; },   // harness: N4's chain precondition (no real drag)
  helpOn: () => helpOn, setHelpOn, queueHelp, helpAfterMove, HELP_ALL: () => HELP_ALL,
  dimOn: () => dimOn, setDimOn: v => { dimOn = v; api.setDimming(v ? 1 : 0); },
  smoothOn: () => smoothOn, setSmoothOn: v => { smoothOn = v; api.setSmooth(v ? 1 : 0); },
  skillArmed: () => skillArmed,
  endDrag: () => endDrag(), setDrag: p => { drag = p ? { path: p, last: null } : null; },
  // harness probe: evaluate the exact draw gate from drawFxLayer (template.html:2665,
  // mirroring Game::Render @0x25924:104-108) over a fake anim. Not game logic.
  dimGate: (v) => {
    const prev = anim;
    anim = { atkDim: v };
    try { return anim.atkDim > 0 && dimOn; } finally { anim = prev; }
  },
};`;

function fire(e, type, x, y) {
  for (const f of (e.listeners[type] || []))
    f({ clientX: x, clientY: y, pointerId: 1, preventDefault() {} });
}
function esc() {
  for (const f of (docL.keydown || [])) f({ key: 'Escape' });
}

(async () => {
  localStorage.m.set('tileRaidRebuildAnim', '0');   // no animation timeline in the shim

  const RIPPED = path.join(__dirname, '../../assets/ripped');
  const fetchOk = (url) => Promise.resolve({
    ok: true,
    json: () => Promise.resolve(String(url).endsWith('sprites.json')
      ? JSON.parse(fs.readFileSync(path.join(RIPPED, 'sprites.json'), 'utf8')) : {}),
  });
  function Image() { this.onload = null; this.onerror = null; }
  Object.defineProperty(Image.prototype, 'src', {
    set(v) { this._src = v; setTimeout(() => this.onload && this.onload(), 0); },
    get() { return this._src; },
  });

  const factory = require(path.join(__dirname, 'dr.js'));
  const run = new Function('window', 'document', 'localStorage', 'performance', 'DR',
    'fetch', 'Audio', 'FontFace', 'FX', 'Image', app + epilogue);
  run(window, document, localStorage, performance, () => factory(),
    fetchOk, function () {}, function () {}, fxMod.exports, Image);
  await new Promise(r => setTimeout(r, 300));   // boot() + ripped art load
  const T = globalThis.__T;
  if (!T) fail('epilogue did not run');
  const api = T.api();

  // ---- settings: help master switch (SetHelpState @0x2505c, UpdateHelpState @0x24fb0) ----
  T.setHelpOn(false);
  if (api.helpEnabled() !== 0) fail('setHelp(false) did not clear Game+0x142f8');
  if (T.helpAvail() !== 0) fail('help OFF must ClearHelp (word 0, UpdateHelpState @0x24fb0)');
  T.setHelpAvail(1 << 4);
  T.queueHelp(4);
  if (T.helpQueue().length) fail('queueHelp passed while the help switch is OFF');
  T.setHelpOn(true);
  if (api.helpEnabled() !== 1) fail('setHelp(true) did not set Game+0x142f8');
  if (T.helpAvail() !== (1 << 4)) fail('help ON with word != 0 must NOT touch the word (UpdateHelpState @0x24fb0)');
  T.setHelpAvail(0);
  T.setHelpOn(true);   // re-ON with word 0 -> ResetHelp (all bits)
  if (T.helpAvail() !== T.HELP_ALL()) fail('help ON with word 0 must ResetHelp (all bits, @0x24fb0)');

  // ---- settings: dimming + smooth engine flags ----
  T.setDimOn(false);
  if (api.dimmingEnabled() !== 0) fail('setDimming(false) did not clear +0x142fa');
  if (T.dimGate(100)) fail('dim quad gate OPEN while attack-dimming is OFF (Render @0x25924:106)');
  T.setDimOn(true);
  if (api.dimmingEnabled() !== 1) fail('setDimming(true) did not set +0x142fa');
  if (!T.dimGate(100)) fail('dim quad gate CLOSED with dimming ON and atkDim=100');
  if (T.dimGate(0)) fail('dim quad gate OPEN with atkDim=0');
  T.setSmoothOn(false);
  if (api.smoothEnabled() !== 0) fail('setSmooth(false) did not clear +0x142fb');
  T.setSmoothOn(true);
  if (api.smoothEnabled() !== 1) fail('setSmooth(true) did not set +0x142fb');
  // A28b RESULT: the flag has NO timing effect in the original (dt = clock() CPU seconds,
  // Game::UpdateTime @0x258e0) — no timing assertion here, flag plumbing only.

  // ---- play harness ----
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
  function legalNbrs(c, r, t, seen) {
    const out = [];
    for (let dc = -1; dc <= 1; dc++) for (let dr = -1; dr <= 1; dr++) {
      if (!dc && !dr) continue;
      const cc = c + dc, rr = r + dr;
      if (cc < 0 || cc > 5 || rr < 0 || rr > 5 || seen.has(cc + rr * 6)) continue;
      const tt = api.tile(cc, rr) & 7;
      if (tt === t || (t === 1 && tt === 4) || (t === 4 && tt === 1)) out.push({ c: cc, r: rr });
    }
    return out;
  }
  function extendPath(start) {
    const t = api.tile(start.c, start.r) & 7;
    const p = [start], seen = new Set([start.c + start.r * 6]);
    for (;;) {
      const cand = legalNbrs(p[p.length - 1].c, p[p.length - 1].r, t, seen);
      if (!cand.length) break;
      cand.sort((a, b) => legalNbrs(b.c, b.r, t, seen).length - legalNbrs(a.c, a.r, t, seen).length);
      const pick = cand[0];
      p.push(pick); seen.add(pick.c + pick.r * 6);
    }
    return p;
  }
  function bestPath() {
    let best = null;
    for (let s = 0; s < 36; s++) {
      const c0 = s % 6, r0 = (s / 6) | 0;
      if ((api.tile(c0, r0) & 7) === 7) continue;
      const p = extendPath({ c: c0, r: r0 });
      if (p.length >= 3 && (!best || p.length > best.length)) best = p;
    }
    return best;
  }
  function playMove() {
    const p = bestPath();
    if (!p) return false;
    T.setDrag(p); T.endDrag();
    return true;
  }
  let games = 0;
  const startGame = () => { T.newGame((0xA240 ^ (games++ * 7919)) >>> 0, 0, 3, 0); };
  startGame();

  // ---- help tip N4: sword chain without a skull (ProcessInput @0x330c0:252) ----
  T.setChainCell({ c: 2, r: 2 });   // harness precondition: N4 anchors at the chain tail tile
  T.setHelpAvail(1 << 4);
  T.helpAfterMove({ sword: true, skull: false, atk: false });
  if (T.helpAvail() & (1 << 4)) fail('N4 bit not cleared (queueHelp clears on show, @0x48348:17)');
  const banner = T.helpBanner();
  if (!T.helpShown()) fail('N4 fired but banner hidden');
  if (!T.helpTip().includes('swords on their own'))
    fail('banner text is not the N4 tip: ' + T.helpTip());
  T.queueHelp(4);
  if (T.helpQueue().length !== 0) fail('N4 re-queued after seen');
  banner.onclick();
  // firing the last unseen tip auto-switches help OFF (Initialise @0x48348:19-21);
  // switch it back ON (ResetHelp) for the BACK test below.
  T.setHelpOn(true);

  // ---- BACK / Escape (BackPressed @0x26a98) ----
  T.setHelpAvail(1 << 4);
  T.queueHelp(4);   // banner up
  if (!T.helpShown()) fail('banner not up before the BACK test');
  esc();
  if (T.helpShown()) fail('BACK did not dismiss the help bubble (@:24-25)');
  if (T.helpQueue().length) fail('BACK left queued tips');

  if (T.curScreen() !== 'scrGame') fail('expected scrGame after startGame, got ' + T.curScreen());
  while (resolveMenu()) {}
  if (api.menu() < 0) {
    esc();
    if (T.curScreen() !== 'scrMenu') fail('in-game BACK did not open the pause menu (MainMenu::FadeIn @:29)');
  } else {
    esc();
    if (T.curScreen() !== 'scrGame') fail('BACK with a menu open must no-op (@:26-28)');
    while (resolveMenu()) {}
    esc();
    if (T.curScreen() !== 'scrMenu') fail('in-game BACK did not open the pause menu (MainMenu::FadeIn @:29)');
  }

  T.showScreen('scrSettings');
  esc();
  if (T.curScreen() !== 'scrMenu') fail('BACK on a sub-screen did not return to the main menu (@:33-50)');
  esc();
  if (T.curScreen() !== 'scrMenu') fail('BACK on the main menu must not navigate (app exit)');

  // ---- skill press/release (ProcessInput @0x49738) ----
  startGame();
  let skillBtn = null;
  for (let t = 0; t < 60 && !skillBtn; t++) {
    while (resolveMenu()) {}
    if (api.over()) { startGame(); continue; }
    playMove();
    while (resolveMenu()) {}
    if (api.stat(21) > 0) skillBtn = document.getElementById('skillBar').children[0];
  }
  if (!skillBtn) fail('no skill button after 60 turns (level-up skill pick)');
  const tipEl = document.getElementById('skillTip');
  // DOWN on a READY skill: tooltip + arm (ProcessInput:50-51,36-40)
  if (api.skill(0, 4) !== 1) { for (let t = 0; t < 40 && api.skill(0, 4) !== 1; t++) { playMove(); while (resolveMenu()) {} } }
  fire(skillBtn, 'pointerdown', 10, 10);
  if (tipEl.hidden) fail('skill tooltip not shown on pointerdown (@:50-51)');
  const ready = api.skill(0, 4) === 1;
  if (ready && T.skillArmed() !== 0) fail('ready skill not armed on pointerdown (@:36-40)');
  if (ready && !skillBtn.classList.contains('held')) fail('ready skill button not highlighted (@:41-47)');
  // UP OUTSIDE the button: nothing (CursorInButton @:88)
  const chargeBefore = api.skill(0, 2);
  efp = null;
  fire(skillBtn, 'pointerup', 999, 999);
  if (T.skillArmed() !== -1) fail('arm not cleared on release');
  if (!tipEl.hidden) fail('tooltip not hidden on release (@:101)');
  if (api.skill(0, 2) !== chargeBefore) fail('skill activated on release OUTSIDE the button');
  // UP on the SAME button while ready: activate (@:84-91)
  fire(skillBtn, 'pointerdown', 10, 10);
  efp = skillBtn;
  fire(skillBtn, 'pointerup', 10, 10);
  if (api.skill(0, 4) === 1 && api.skill(0, 2) === chargeBefore)
    fail('skill NOT activated on release on the same button (@:88-91)');
  // DOWN on a COOLING skill: tooltip only, no arm (@:36-38 gate)
  if (api.skill(0, 4) === 1) { for (let t = 0; t < 40 && api.skill(0, 4) === 1; t++) { playMove(); while (resolveMenu()) {} } }
  if (api.skill(0, 4) === 1) fail('could not get a cooling skill for the arm-gate test');
  fire(skillBtn, 'pointerdown', 10, 10);
  if (tipEl.hidden) fail('cooling skill: tooltip not shown on pointerdown (@:50-51)');
  if (T.skillArmed() !== -1) fail('cooling skill armed (ready gate @:36-38)');
  if (skillBtn.classList.contains('held')) fail('cooling skill highlighted');
  efp = skillBtn;
  fire(skillBtn, 'pointerup', 10, 10);
  if (api.skill(0, 4) === 1) fail('cooling skill activated on release (CanActivate @:89-91)');

  // ---- help tip N14: health ratio <= 0.35 (ProcessHeartbeat @0x48dfc:26, DAT_00048f20) ----
  let low = false;
  for (let t = 0; t < 800 && !low; t++) {
    while (resolveMenu()) {}
    if (api.over()) { startGame(); continue; }
    playMove();
    while (resolveMenu()) {}
    const hp = api.stat(0), mhp = api.stat(1);
    if (mhp > 0 && hp * 20 <= mhp * 7) low = true;
  }
  if (!low) fail('health never reached ratio <= 0.35 in 800 turns (hp ' + api.stat(0) + '/' + api.stat(1) + ')');
  T.setHelpOn(true);   // the BACK test's tip exhausted the word and auto-switched help OFF
  T.setHelpAvail(1 << 14);
  T.helpAfterMove({});
  if (T.helpAvail() & (1 << 14)) fail('N14 bit not cleared at ratio <= 0.35');
  if (!T.helpShown()) fail('N14 fired but banner hidden');
  if (!T.helpTip().includes('low')) fail('banner text is not the N14 tip: ' + T.helpTip());

  console.log('PASS: A24 BACK routing, skill press/release, help/dim/smooth settings, tips N4+N14');
})().catch(e => fail(e.stack || String(e)));
