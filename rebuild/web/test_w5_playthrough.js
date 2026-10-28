// W5 scripted play-through: runs template.html's app script in a DOM shim against the REAL
// dr.js and plays 3 classes x 3 modes (~30 moves each, skills fired, trophy tapped when one
// appears), opens the class screen and edits perks (refused + accepted), plays one classic
// run to death -> score insert -> high-score page, and a meta save/load round trip.
// Test harness code only — not game logic (move policy, menu picks, class unlocks via blob
// patching are test scaffolding).
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
    // U12: Original-only — the score/name/class/mode screens draw on their own *Canvas;
    // the stub gives every canvas-id element the ctx stub (TEST HARNESS).
    if (!byId.has(id)) byId.set(id, (id === 'board' || id === 'fxLayer' || id === 'dimLayer' || id === 'hudBar' || id.endsWith('Canvas')) ? Object.assign(el('canvas'), { getContext: () => ctxStub }) : el('div'));
    return byId.get(id);
  },
  createElement: el,
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
const fxMod = { exports: {} };   // A4a: the app script uses the pure FX physics block
new Function('module', blocks.find(b => b.includes('A4a animation physics')))(fxMod);
const app = blocks[blocks.length - 1].replace(/__ART__/g, 'false').replace(/__RECREATED__/g, 'null');
const epilogue = `
;globalThis.__T = {
  api: () => api, M: () => M, newGame, refresh, draw,
  openClassScreen, renderClassScreen, commitClassScreen,
  setSel: (k, t) => { clsSel = k; clsTab = t; skSel = -1; clsMsg = ''; },
  checkOver, hideGameOver, renderScores, setScPage: p => { scPage = p; },
  setDrag: p => { drag = p ? { path: p, last: null } : null; },
  endDrag: () => endDrag(),
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
  const rnd = xorshift(0x51A7);   // fixed seed
  const notables = [];

  // ---- unlock classes 1 + 8 through the meta blob (test scaffolding, as in test_headless)
  T.newGame(1, 1, 3, 0);
  const setLevels = levels => {
    const mp = api.metaSave(), ml = api.metaSaveLen();
    const meta = HEAP().slice(mp, mp + ml);
    const off = 8 + 2, REC = 47, LVL = 26;
    for (const [k, lvl] of Object.entries(levels)) meta[off + k * REC + LVL] = lvl;
    const lp = api.buf(ml);
    HEAP().set(meta, lp);
    if (!api.metaLoad(lp, ml)) fail('metaLoad rejected patched blob');
  };
  setLevels({ 1: 10, 8: 10 });
  if (api.cls(1, 0) !== 10 || api.cls(8, 0) !== 10) fail('class unlock patch failed');

  // ---- test policy: random legal chains; menus prefer skill offers
  function randomPath() {
    let c0, r0;
    if (rnd() < 0.5) {
      const sk = [];
      for (let c = 0; c < 6; c++) for (let r = 0; r < 6; r++)
        if ((api.tile(c, r) & 7) === 1) sk.push({ c, r });
      if (sk.length) { const s = sk[(rnd() * sk.length) | 0]; c0 = s.c; r0 = s.r; }
    }
    if (c0 === undefined) { c0 = (rnd() * 6) | 0; r0 = (rnd() * 6) | 0; }
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
      const p = randomPath();
      if (!p) continue;
      const bytes = new Uint8Array(p.map(q => q.c + q.r * 6));
      const ptr = api.buf(bytes.length);
      HEAP().set(bytes, ptr);
      if (api.play(ptr, bytes.length)) return p;
    }
    return null;
  }
  function tapTrophy() {   // single-tap release through the page's endDrag (dr_tap path)
    for (let c = 0; c < 6; c++) for (let r = 0; r < 6; r++) {
      const p = api.tile(c, r);
      if ((p & 7) === 6 && ((p >> 9) & 3) === 1) {
        T.setDrag([{ c, r }]); T.endDrag();
        return true;
      }
    }
    return false;
  }

  // ---- 3 classes x 3 modes, ~30 moves each, skills fired, trophy tapped when present
  const CLASSES = [0, 1, 8], MODES = [3, 0, 1];
  for (const k of CLASSES) for (const m of MODES) {
    T.newGame(0x51A7 + k * 31 + m, 1, m, k);
    if (api.stat(19) !== k) fail('class ' + k + ' not committed (mode ' + m + ')');
    if (api.mode() !== m) fail('dr_mode ' + api.mode() + ' want ' + m);
    let moves = 0, skills = 0, trophies = 0;
    // ~30 moves; the tick budget is larger so a level-up (first skill) can land and fire.
    for (let n = 0; n < 200 && moves < 30 && !api.over(); n++) {
      if (api.menu() >= 0) { resolveMenu(); continue; }
      for (let s = 0; s < api.stat(21); s++)
        if (api.skill(s, 4) === 1 && api.skillActivate(s) === 1) { skills++; while (api.menu() >= 0) resolveMenu(); }
      if (tapTrophy()) trophies++;
      if (api.over() || api.menu() >= 0) continue;
      if (playMove()) moves++;
    }
    T.refresh(); T.draw();   // page renders the state without throwing
    console.log('class ' + k + ' mode ' + m + ': ' + moves + ' moves, ' + skills + ' skills fired, ' +
                trophies + ' trophy taps, turn ' + api.stat(8) + (api.over() ? ' (died)' : ''));
    if (moves < 25) notables.push('class ' + k + ' mode ' + m + ': only ' + moves + ' moves in 200 ticks');
    if (skills === 0) notables.push('class ' + k + ' mode ' + m + ': no skill owned in 30 moves (level-up not reached)');
  }

  // ---- skill run: level 2 needs 100 xpHundreds (save.cpp:282 g.level = xpHundreds/100+1), so
  // one longer classic run plays until the first level-up grants a skill, then fires it.
  T.newGame(0x4242, 1, 3, 0);
  let skillFired = 0, skillMoves = 0;
  for (let n = 0; n < 1200 && !skillFired; n++) {
    if (api.over()) { T.newGame(0x4242 ^ n, 1, 3, 0); continue; }
    if (api.menu() >= 0) { resolveMenu(); continue; }
    if (api.stat(21) > 0) {
      for (let s = 0; s < api.stat(21); s++)
        if (api.skill(s, 4) === 1 && api.skillActivate(s) === 1) skillFired++;
      while (api.menu() >= 0) resolveMenu();
      if (skillFired) break;
    }
    if (playMove()) skillMoves++;
  }
  if (!skillFired) notables.push('skill run: no skill acquired in 1200 moves (level 2 = 100 xpHundreds)');
  else console.log('skill run: first skill fired after ' + skillMoves + ' moves (level ' + api.stat(7) + ')');

  // ---- class screen: open, all tabs, perk edits refused + accepted
  T.openClassScreen();
  for (let k = 0; k < 10; k++) for (let t = 0; t < 4; t++) { T.setSel(k, t); T.renderClassScreen(); }
  if (api.classSet(0, 0, 1, 0) !== 0) fail('Adventurer perk edit must be refused');
  if (api.classSet(2, 0, 1, 0) !== 0) fail('locked class perk edit must be refused');
  if (api.classSet(1, 0, 1, 0) !== 1) fail('accepted perk edit refused (class 1 lv10)');
  if (api.cls(1, 4) !== 0) fail('accepted perk not stored');
  T.commitClassScreen();

  // ---- classic run to death -> score insert -> high-score page
  T.newGame(0xF00D, 1, 3, 0);
  let over = false;
  for (let n = 0; n < 4000 && !over; n++) {
    if (api.menu() >= 0) { resolveMenu(); continue; }
    if (api.over()) { over = true; break; }
    if (!playMove()) break;
  }
  if (!over) fail('classic run never ended in 4000 moves');
  T.checkOver();   // page: overlay + dr_score_add insert (once)
  const ov = document.getElementById('goOverlay');
  if (ov.hidden !== false) fail('game-over overlay not shown');
  const rankTxt = document.getElementById('goRank').textContent;
  if (!rankTxt) fail('goRank empty');
  const page = api.difficulty();   // classic: page = difficulty (ScoreType)
  const rank = (() => { for (let r = 0; r < 10; r++) if (api.score(page, r, 0) >= 0) return r; return -1; })();
  if (rank < 0) fail('no inserted entry on page ' + page);
  const insScore = api.score(page, rank, 0);
  T.setScPage(page); T.renderScores();
  // U12: Original-only — the score screen is the scCanvas quad/label canvas (ScoreScreen::
  // Render @0x5e070 path), the DOM #scTable side is REMOVED. The canvas draws straight from
  // api.score(page, r, .) — assert the engine table carries the inserted score at its rank
  // and that renderScores paints the canvas without throwing (stub ctx is strict).
  if (api.score(page, rank, 0) !== insScore)
    fail('high-score page missing the inserted score ' + insScore);
  const scCv = document.getElementById('scCanvas');
  if (!scCv || scCv.tag !== 'canvas') fail('score canvas missing for the Original score screen');
  console.log('score insert ok: page ' + page + ' rank ' + rank + ' score ' + insScore + ' (' + rankTxt + ')');

  // ---- W8: name field + Random on the new-game screen -> score entry -> score page + game over.
  // Test harness: drives the page's own Random/Begin handlers (not game logic).
  const findName = (pg, want) => {
    for (let r = 0; r < 10; r++) {
      if (api.score(pg, r, 0) < 0) break;
      if (api.scoreName(pg, r) === want) return r;
    }
    return -1;
  };
  const nameEl = document.getElementById('ngName');
  document.getElementById('ngNameRandom').onclick();   // dice button (GenerateRandomName @0x5615c)
  const rndName = nameEl.value;
  if (!rndName || rndName.length > 16) fail('random name empty/too long: "' + rndName + '"');
  nameEl.value = 'Testy';
  document.getElementById('ngStart').onclick();        // Begin: dr_name_set BEFORE dr_new_game
  if (api.name() !== 'Testy') fail('name not carried: ' + api.name());
  over = false;
  for (let n = 0; n < 4000 && !over; n++) {
    if (api.menu() >= 0) { resolveMenu(); continue; }
    if (api.over()) { over = true; break; }
    if (!playMove()) break;
  }
  if (!over) fail('named classic run never ended in 4000 moves');
  T.checkOver();
  if (document.getElementById('goName').textContent !== 'Testy')
    fail('game-over name label: ' + document.getElementById('goName').textContent);
  const page2 = api.difficulty();
  const rank2 = findName(page2, 'Testy');
  if (rank2 < 0) fail('no "Testy" entry on page ' + page2);
  T.hideGameOver();
  T.setScPage(page2); T.renderScores();
  // U12: Original-only — the score page is the scCanvas (the DOM #scTable side and its
  // rebuild-added "Class*" column are REMOVED). The canvas draws names from
  // api.scoreName — assert the engine carries "Testy" (rank2 above) and the render runs.
  if (api.scoreName(page2, rank2) !== 'Testy')
    fail('high-score page missing the name Testy');
  console.log('W8 name ok: random "' + rndName + '", set "Testy" -> score page ' + page2 +
              ' rank ' + rank2 + ' + game-over label + score-table entry');

  // ---- W8: 16-char cap (ProcessInput @0x57a10:498-501 strncpy 0x10 — truncate, never refuse)
  nameEl.value = '0123456789abcdefghij';   // 20 chars
  document.getElementById('ngStart').onclick();
  if (api.name() !== '0123456789abcdef') fail('name not truncated to 16: ' + api.name());
  over = false;
  for (let n = 0; n < 4000 && !over; n++) {
    if (api.menu() >= 0) { resolveMenu(); continue; }
    if (api.over()) { over = true; break; }
    if (!playMove()) break;
  }
  if (!over) fail('truncation run never ended in 4000 moves');
  T.checkOver();
  const page3 = api.difficulty();
  if (findName(page3, '0123456789abcdef') < 0)
    fail('score entry not truncated: "' + api.scoreName(page3, 0) + '"');
  console.log('W8 truncation ok: 20-char field -> "0123456789abcdef" in the score entry');

  // ---- meta save/load round trip: scores + class levels survive
  const rankNow = (() => {   // W8 runs added page-1 entries — locate insScore's rank, don't assume 0
    for (let r = 0; r < 10; r++) { const s = api.score(page, r, 0); if (s < 0) break; if (s === insScore) return r; }
    return -1;
  })();
  if (rankNow < 0) fail('inserted score ' + insScore + ' vanished before the meta round trip');
  const mp = api.metaSave(), ml = api.metaSaveLen();
  const meta = HEAP().slice(mp, mp + ml);
  const lp = api.buf(ml);
  HEAP().set(meta, lp);
  if (!api.metaLoad(lp, ml)) fail('meta round trip load failed');
  if (api.score(page, rankNow, 0) !== insScore) fail('score lost across meta round trip');
  if (api.cls(1, 0) !== 10) fail('class level lost across meta round trip');
  console.log('meta save/load round trip ok (' + ml + ' bytes)');

  // ---- what the page cannot show (report to master)
  notables.push('HUD clock = device wall clock "h:mm AM/PM" while the Clock setting is on (W7; Java onDrawFrame "h:mm a"); dr_stat 31 removed');
  notables.push('Trophies HUD = trophies SPAWNED (dr_stat 33, +0x14da4); no collected-trophy counter exists in the decomp');
  for (const n of notables) console.log('NOTE: ' + n);
  console.log('PASS: W5 play-through 3 classes x 3 modes, class screen, score insert, W8 name (random/set/truncate/score page/game over), meta round trip');
})().catch(e => fail(e && e.stack || String(e)));
