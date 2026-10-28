// A22 node test: enemy-tooltip triggers + help tips 0x15/0x16, against the REAL wasm module.
// Covers task A22 items 1, 2, 5:
//   1. the tooltip opens on touch-DOWN only (no pointermove hover) — Game::ProcessInput
//      @0x330c0:92-108 (state 1 from Game::TouchStart @0x2442c); it fades on drag-off
//      (ProcessTileSelection @0x2c8dc:63) and on release (Interface::ProcessInput @0x49738:101);
//   2. a type-32 enemy gets no tooltip while its countdown (tile+0x260) != 0 —
//      ProcessInput @0x330c0:97;
//   5. help 0x15 fires once when an enemy tile with kind != 0 is on the board
//      (Game::EndOfTurn @0x3289c:317-321, seen-bitmask HelpMenu::Initialise @0x48348),
//      and help 0x16 on the first chain matching a special enemy (MatchTile @0x2ac40:225,286).
// Test harness code only — not game logic (move policy, menu picks).
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
      // mini shim: an innerHTML string's class="x" divs become children so the menu-card
      // querySelector('.ot')/querySelectorAll('.oi') wiring works. Harness only.
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
const document = {
  addEventListener() {},   // A24: BACK/Escape keydown handler (template.html)
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

function xorshift(s) {
  let x = s >>> 0 || 1;
  return () => { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; };
}

const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
const fxMod = { exports: {} };
new Function('module', blocks.find(b => b.includes('A4a animation physics')))(fxMod);
const app = blocks[blocks.length - 1]
  .replace(/__ART__/g, 'true').replace(/__RECREATED__/g, 'null')
  .replace(/__ASSET_BASE__/g, '../../assets/ripped');
const epilogue = `
;globalThis.__T = {
  api: () => api, M: () => M, newGame, draw,
  enemyTipCtl: () => enemyTipCtl, enemyTipAllowed,
  helpAvail: () => helpAvail,
  setHelpAvail: v => { helpAvail = v >>> 0; api.setHelpState(helpAvail); },
  helpQueue: () => helpQueue,
  helpFlush: () => showHelpNext(),   // U13 review C: page's deferred-tip flush (close path)
  // F5: Original draws the banner on #fxLayer (helpDraw), DOM element stays hidden —
  // assert the theme-abstract state; helpKill mirrors the game-over/back dismissal.
  helpShown: () => helpShowing(),
  helpKill: () => { helpDraw = null; helpBanner.hidden = true; helpPaint(); },
  helpBanner: () => document.getElementById('helpBanner'),
  // U12: Original-only — the banner is the CANVAS bubble (helpDraw); its text is the
  // HELP_TIPS entry. TEST HARNESS: read the tip from the canvas state, not the DOM element.
  helpTip: () => (helpDraw ? HELP_TIPS[helpDraw.n] : ''),
  endDrag: () => endDrag(), setDrag: p => { drag = p ? { path: p, last: null } : null; },
  boardDy: () => boardDy,
};`;

function fire(e, type, x, y) {
  for (const f of (e.listeners[type] || []))
    f({ clientX: x, clientY: y, pointerId: 1, preventDefault() {} });
}

(async () => {
  // A24: the seen bitmask lives in the engine/save (HelpMenu+0x38); a new game resets it
  // to all-unseen (ResetHelp @0x47458). Seed it to ONLY bits 21 (help N21) + 22 (N22) so
  // only those two tips can fire during the run (queueHelp @ template.html).
  localStorage.m.set('tileRaidRebuildAnim', '0');   // no animation timeline in the shim

  // ART=true -> the Original theme is the default (origHud() true, the tooltip exists).
  // fetch serves the real ripped sprites.json from disk; the Image stub settles the png
  // loads like test_themes.js does. Test harness only.
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

  // ---- item 2 (unit): the open gate, ProcessInput @0x330c0:97,105 ----
  const pack = (type, kind) => (kind << 3) | type;
  if (T.enemyTipAllowed(pack(1, 0), 0)) fail('kind-0 enemy must not open the tooltip (ProcessInput:105)');
  if (!T.enemyTipAllowed(pack(1, 5), 0)) fail('kind-5 enemy must open the tooltip');
  if (!T.enemyTipAllowed(pack(1, 32), 0)) fail('kind-32 with countdown 0 must open (ProcessInput:97)');
  if (T.enemyTipAllowed(pack(1, 32), 7)) fail('kind-32 with countdown != 0 must NOT open (ProcessInput:97)');
  if (T.enemyTipAllowed(pack(2, 5), 0)) fail('non-enemy tile must not open the tooltip');

  // ---- play classic runs until a special enemy (kind != 0) is on the board. The first
  // natural special lands ~turn 50 (InitNewGame @0x2d7a4:54-63 countdown 50, EndOfTurn
  // @0x3289c:286-290 decrement, SetType @0x64ef8 -> Promote @0x64e3c at 0), then every
  // GenerateStats timer (@0x63098 -> SetTimeToPromotion @0x2985c).
  const rnd = xorshift(0xA228);
  const canvas = document.getElementById('board');
  const cellPx = 80, dy = T.boardDy();
  const cx = c => c * cellPx + cellPx / 2, cy = r => dy + r * cellPx + cellPx / 2;
  let games = 0;
  // A24: newGame resets the seen bits to all-unseen (helpNewGame -> ResetHelp @0x47458);
  // re-seed to ONLY bits 21/22 after every start so just those two tips can fire.
  const startGame = () => {
    T.newGame((0xA228 ^ (games++ * 7919)) >>> 0, 0, 3, 0);
    T.setHelpAvail((1 << 21) | (1 << 22));
  };
  startGame();

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
    // U13 review C: the page flushes help tips deferred while a panel was up on its close
    // path (closeMenu/checkMenu/drainSounds); this harness closes via api.choose directly,
    // so it pumps the page's flush here. Harness policy, not game logic.
    T.helpFlush();
    return true;
  }
  function specialOnBoard() {
    for (let c = 0; c < 6; c++) for (let r = 0; r < 6; r++) {
      const p = api.tile(c, r);
      if ((p & 7) === 1 && ((p >> 3) & 63) !== 0) return { c, r, kind: (p >> 3) & 63 };
    }
    return null;
  }
  // greedy longest legal chain (harness policy, not game logic)
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

  let seen21 = false;
  for (let mv = 0; mv < 400 && !seen21; mv++) {
    while (resolveMenu()) {}
    if (api.over()) { startGame(); continue; }
    playMove();
    while (resolveMenu()) {}
    if (!(T.helpAvail() & (1 << 21))) seen21 = true;   // bit cleared = tip shown
  }
  if (!seen21) fail('help N21 never fired after ' + games + ' games (turn ' + api.stat(8) +
    ', special on board: ' + JSON.stringify(specialOnBoard()) + ')');

  // ---- item 5: N21 fired once, the canvas banner carries the N21 text ----
  if (!T.helpShown()) fail('help banner hidden although N21 just fired');
  if (!T.helpTip().includes('This is a special enemy'))
    fail('banner text is not the N21 tip: ' + T.helpTip());
  if (!specialOnBoard()) fail('N21 fired but no special enemy on the board (EndOfTurn @0x3289c:319)');

  // ---- item 1: tooltip opens on touch-down only, not on hover (special is fresh) ----
  while (resolveMenu()) {}
  let sp = specialOnBoard();
  for (let t = 0; t < 30 && (!sp || api.over() || api.menu() >= 0); t++) {
    if (api.over()) startGame();
    playMove();
    while (resolveMenu()) {}
    sp = specialOnBoard();
  }
  if (!sp || api.over() || api.menu() >= 0) fail('no live special enemy for the tooltip test');
  const ctl = T.enemyTipCtl();
  // hover (pointermove without pointerdown) must not open it
  fire(canvas, 'pointermove', cx(sp.c), cy(sp.r));
  if (ctl.visible()) fail('tooltip opened on pointermove hover (must be touch-down only)');
  // touch-down on the special enemy opens it
  fire(canvas, 'pointerdown', cx(sp.c), cy(sp.r));
  if (!ctl.visible()) fail('tooltip did not open on pointerdown over a special enemy');
  // the band carries the name line + description lines
  const tipEl = document.getElementById('enemyTip');
  if (tipEl.hidden) fail('tooltip element still hidden after touch-down');
  if (!tipEl.children.some(ch => ch.className === 'etn')) fail('tooltip has no name line');
  // drag onto a different tile fades it (ProcessTileSelection @0x2c8dc:63)
  const other = sp.c > 0 ? { c: sp.c - 1, r: sp.r } : { c: sp.c + 1, r: sp.r };
  fire(canvas, 'pointermove', cx(other.c), cy(other.r));
  if (ctl.visible()) fail('tooltip still visible after the drag moved to a different tile');
  // release fades it (Interface::ProcessInput @0x49738:101)
  fire(canvas, 'pointerdown', cx(sp.c), cy(sp.r));
  if (!ctl.visible()) fail('tooltip did not re-open on a second touch-down');
  fire(canvas, 'pointerup', cx(sp.c), cy(sp.r));
  if (ctl.visible()) fail('tooltip still visible after pointerup (fadeHide)');
  // a kind-0 enemy never opens it (ProcessInput @0x330c0:105)
  for (let c = 0; c < 6; c++) for (let r = 0; r < 6; r++) {
    const p = api.tile(c, r);
    if ((p & 7) === 1 && ((p >> 3) & 63) === 0) {
      fire(canvas, 'pointerdown', cx(c), cy(r));
      if (ctl.visible()) fail('kind-0 enemy opened the tooltip');
      fire(canvas, 'pointerup', cx(c), cy(r));
      c = 6; break;
    }
  }

  // ---- seen-bitmask once-only for N21: keep playing, it must not re-queue ----
  T.helpKill();   // U12: dismiss the canvas banner (DOM element never shows)
  T.setHelpAvail(0);   // every tip seen: any banner now would be a re-queue bug
  for (let t = 0; t < 10; t++) {
    while (resolveMenu()) {}
    if (api.over()) { startGame(); T.setHelpAvail(0); continue; }
    playMove();
    while (resolveMenu()) {}
  }
  if (T.helpQueue().length) fail('help N21 re-queued after seen (queueHelp bitmask, HelpMenu::Initialise @0x48348)');
  if (T.helpShown()) fail('help banner re-shown although every other tip is seen');

  // ---- item 5b: help N22 on the first chain that matches a special enemy ----
  T.setHelpAvail(1 << 22);
  T.helpKill();
  let fired22 = false;
  for (let t = 0; t < 400 && !fired22; t++) {
    while (resolveMenu()) {}
    if (api.over()) { startGame(); T.setHelpAvail(1 << 22); continue; }
    const s = specialOnBoard();
    let p = null;
    if (s) {   // harness policy: start the chain ON the special enemy, extend with neighbours
      p = extendPath({ c: s.c, r: s.r });
      if (p.length < 3) p = null;
    }
    if (!p) { playMove(); continue; }
    T.setDrag(p); T.endDrag();
    while (resolveMenu()) {}
    if (!(T.helpAvail() & (1 << 22))) fired22 = true;
  }
  if (!fired22) fail('help N22 never fired after chains through special enemies (MatchTile @0x2ac40:286)');
  if (!T.helpShown()) fail('help N22 fired but banner hidden');
  if (!T.helpTip().includes('Killing special enemies'))
    fail('banner text is not the N22 tip: ' + T.helpTip());

  console.log('PASS: A22 tooltip touch-down-only + type-32 gate + fade triggers; help N21 once; help N22 on special match');
})().catch(e => fail(e.stack || String(e)));
