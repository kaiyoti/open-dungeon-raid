// W9 node test: the Game must exist BEFORE the first game. On a fresh module (no dr_new_game
// run yet) every menu/meta export must answer the way the original does right after
// Game ctor + Game::Start @0x260ac (CharacterClass::Initialise for all 10, :168-172, then the
// classes blob load at :174-176). The bug this covers: with no Game, dr_class() returned -1,
// which the new-game class picker read as "not locked" plus RACE_NAMES[-1] = "undefined".
// Test harness code only — not game logic.
'use strict';
const fs = require('fs');
const path = require('path');

function fail(msg) { console.error('FAIL: ' + msg); process.exit(1); }

// CharacterClass::Initialise @0x36524 / analysis/features/CLASSES.md §1 — race per class 0..9.
const WANT_RACE = [0, 1, 6, 4, 5, 9, 3, 8, 2, 7];

function bind(M) {
  const w = (ret, args, name) => {
    if (!(typeof M['_' + name] === 'function' || (M.asm && name in M.asm)))
      fail('missing wasm export ' + name);
    return M.cwrap(name, ret, args);
  };
  return {
    init:        w('number', [], 'dr_init'),
    cls:         w('number', ['number', 'number'], 'dr_class'),
    metaSave:    w('number', [], 'dr_meta_save'),
    metaSaveLen: w('number', [], 'dr_meta_save_len'),
    metaLoad:    w('number', ['number', 'number'], 'dr_meta_load'),
    buf:         w('number', ['number'], 'dr_buf'),
    newGame:     w(null, ['number', 'number', 'number', 'number'], 'dr_new_game'),
    mode:        w('number', [], 'dr_mode'),
    difficulty:  w('number', [], 'dr_difficulty'),
    lifetime:    w('number', ['number'], 'dr_lifetime'),
    score:       w('number', ['number', 'number', 'number'], 'dr_score'),
    stat:        w('number', ['number'], 'dr_stat'),
    over:        w('number', [], 'dr_over'),
  };
}

// Class meta every state must keep: Adventurer unlocked, the race table, inherent = chosen race.
function checkRaces(api, where) {
  if (api.cls(0, 0) !== 1) fail(where + ': class 0 level = ' + api.cls(0, 0) + ', want 1 (Adventurer)');
  for (let k = 0; k < 10; k++) {
    const r = api.cls(k, 1), inh = api.cls(k, 2);
    if (r !== WANT_RACE[k]) fail(where + ': class ' + k + ' race = ' + r + ', want ' + WANT_RACE[k]);
    if (inh !== r) fail(where + ': class ' + k + ' inherent race = ' + inh + ', want race ' + r);
  }
  if (api.cls(99, 0) !== -1 || api.cls(0, 99) !== -1) fail(where + ': bad dr_class args must still be -1');
}

// Only a pristine (never patched, never played) class set: classes 1..9 locked and the
// level-derived fields at their level-1 / level-0 values.
function checkPristine(api, where) {
  for (let k = 1; k < 10; k++) {
    if (api.cls(k, 0) !== 0) fail(where + ': class ' + k + ' level = ' + api.cls(k, 0) + ', want 0 (locked)');
    // field 13 = Game::IsRaceUnlocked @0x248a0: Human belongs to the Adventurer (always), every
    // other race needs its class at level > 4 — so all of them start locked.
    if (api.cls(k, 13) !== 0) fail(where + ': class ' + k + ' race choice must start locked');
    // field 11 = unlocked perk/flaw slots (CharacterClass::IsPerkUnlocked @0x35d78),
    // field 12 = replacement budget (@0x35dc0): level 0 gives 0/0.
    if (api.cls(k, 11) !== 0 || api.cls(k, 12) !== 0) fail(where + ': class ' + k + ' perk/budget must start empty');
    // field 54/55 = Games Played / Enemies Killed (CheckEndGame @0x2f960:61-63)
    if (api.cls(k, 54) !== 0 || api.cls(k, 55) !== 0) fail(where + ': class ' + k + ' run counters must start at 0');
  }
  if (api.cls(0, 11) !== 1) fail(where + ': Adventurer must have its slot 0 unlocked (level != 0)');
  if (api.cls(0, 12) !== 0) fail(where + ': Adventurer replacement budget must be 0 at level 1');
  if (api.cls(0, 13) !== 1) fail(where + ': Human race choice must be unlocked (Adventurer)');
}

// The other menu exports the task lists (dr_score / dr_lifetime / dr_mode / dr_difficulty).
function checkMenus(api, where) {
  if (api.mode() !== 3) fail(where + ': dr_mode = ' + api.mode() + ', want 3 (MODE_CLASSIC, game.h:43)');
  if (api.difficulty() !== 1) fail(where + ': dr_difficulty = ' + api.difficulty() + ', want 1 (DIFF_NORMAL)');
  if (api.score(0, 0, 0) !== -1) fail(where + ': empty score slot must read -1');
  if (!Number.isFinite(api.lifetime(0)) || !Number.isFinite(api.lifetime(14)))
    fail(where + ': dr_lifetime must answer before a game');
  if (api.over() !== 0) fail(where + ': no game is over yet');
}

(async () => {
  const factory = require(path.join(__dirname, 'dr.js'));

  // ---- 1. fresh module: no dr_init call, no game — the module start alone must do it -------
  const M1 = await factory();
  const api1 = bind(M1);
  checkRaces(api1, 'fresh module');
  checkPristine(api1, 'fresh module');
  checkMenus(api1, 'fresh module');
  if (api1.stat(19) !== 0) fail('fresh module: activeClass must start at 0');

  // ---- 2. meta save -> reload -> same ------------------------------------------------------
  const mp = api1.metaSave(), ml = api1.metaSaveLen();
  if (!mp || ml < 4 + 4 + 472 + 4 + 5 + 6 * 10 * 60) fail('dr_meta_save produced ' + ml + ' bytes');
  const blob = M1.HEAPU8.slice(mp, mp + ml);
  if (blob[0] !== 0x44 || blob[1] !== 0x52) fail('meta container magic missing');
  const clsLen = blob[4] | (blob[5] << 8) | (blob[6] << 16) | (blob[7] << 24);
  if (clsLen !== 472) fail('meta classes payload = ' + clsLen + ', want 472 (2 B + 10 x 47 B)');

  const off = 8 + 2, REC = 47, LVL = 26;
  const patched = blob.slice(0);
  patched[off + 1 * REC + LVL] = 10;                 // unlock class 1 through the saved blob
  const p1 = api1.buf(ml);
  M1.HEAPU8.set(patched, p1);
  if (!api1.metaLoad(p1, ml)) fail('dr_meta_load rejected the patched blob');
  if (api1.cls(1, 0) !== 10) fail('dr_meta_load did not apply the patched level');
  if (api1.init() !== 1) fail('dr_init must answer 1');
  if (api1.cls(1, 0) !== 10) fail('dr_init rolled the loaded classes back');
  if (!api1.metaLoad(p1, ml)) fail('dr_meta_load round trip failed');
  if (api1.cls(1, 0) !== 10) fail('levels lost across a meta reload');
  console.log('meta save -> reload -> same ok (' + ml + ' bytes)');

  // ---- 3. a genuinely fresh module (page reload) loads the same blob -----------------------
  const M2 = await factory();
  const api2 = bind(M2);
  checkRaces(api2, 'second fresh module');
  checkPristine(api2, 'second fresh module');
  checkMenus(api2, 'second fresh module');
  const p2 = api2.buf(ml);
  M2.HEAPU8.set(patched, p2);
  if (!api2.metaLoad(p2, ml)) fail('dr_meta_load rejected the blob on a fresh module');
  if (api2.cls(1, 0) !== 10) fail('fresh module did not load the saved class level');
  console.log('fresh module loads the saved meta ok');

  // ---- 4. then a game: dr_class must still be right ----------------------------------------
  api1.newGame(9, 1, 3, 0);                          // classic run as the Adventurer
  checkRaces(api1, 'after dr_new_game');
  checkMenus(api1, 'after dr_new_game');
  if (api1.cls(1, 0) !== 10) fail('dr_new_game dropped the loaded class level');
  if (api1.stat(19) !== 0) fail('dr_new_game did not commit class 0');
  api1.newGame(10, 1, 0, 1);                         // Dungeon Sprint as class 1 (unlocked above)
  if (api1.stat(19) !== 1) fail('dr_new_game did not commit class 1');
  checkRaces(api1, 'after the second dr_new_game');
  if (api1.cls(0, 0) !== 1 || api1.cls(1, 0) !== 10 || api1.cls(9, 0) !== 0)
    fail('dr_class wrong after the second game');
  console.log('dr_new_game keeps the class meta ok');

  // ---- 5. template.html boot order: init -> dr_meta_load -> main menu ----------------------
  const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
  const app = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]).pop();
  if (!app.includes("'dr_init'")) fail('template.html does not bind dr_init');
  const boot = app.slice(app.indexOf('(async function boot'));
  const iInit = boot.indexOf('api.init()'), iLoad = boot.indexOf('api.metaLoad('), iMenu = boot.indexOf('toMenu()');
  if (iInit < 0) fail('boot() does not call api.init()');
  if (iLoad < 0) fail('boot() does not call api.metaLoad');
  if (iMenu < 0) fail('boot() does not show the main menu');
  if (!(iInit < iLoad && iLoad < iMenu)) fail('boot order must be init -> dr_meta_load -> toMenu');
  console.log('template.html boot order ok (init -> dr_meta_load -> toMenu)');
  console.log('PASS: W9 boot/meta state on a fresh module');
})().catch(e => { console.error('FAIL: ' + (e && e.stack || e)); process.exit(1); });
