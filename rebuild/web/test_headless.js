// Headless smoke test for web/dr.js (W1): loads the WASM module in Node, plays a few
// hundred random legal moves through the exported API, resolves every reward menu,
// and checks one save -> load -> save round trip. No crash + sane returns = pass.
//   node test_headless.js [turns]
'use strict';
const path = require('path');

function fail(msg) { console.error('FAIL: ' + msg); process.exit(1); }

function xorshift(s) {  // deterministic RNG for the test policy
  let x = s >>> 0 || 1;
  return () => { x ^= x << 13; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; };
}

(async () => {
  const factory = require(path.join(__dirname, 'dr.js'));
  const M = await factory();
  const w = (ret, args, name) => M.cwrap(name, ret, args);
  const api = {
    new: w(null, ['number', 'number'], 'dr_new'),
    tile: w('number', ['number', 'number'], 'dr_tile'),
    enemy: w('number', ['number', 'number', 'number'], 'dr_enemy'),
    stat: w('number', ['number'], 'dr_stat'),
    skill: w('number', ['number', 'number'], 'dr_skill'),
    play: w('number', ['number', 'number'], 'dr_play'),
    menu: w('number', [], 'dr_menu'),
    offer: w('number', ['number', 'number'], 'dr_menu_offer'),
    choose: w(null, ['number', 'number'], 'dr_choose'),
    over: w('number', [], 'dr_over'),
    save: w('number', [], 'dr_save'),
    saveLen: w('number', [], 'dr_save_len'),
    buf: w('number', ['number'], 'dr_buf'),
    load: w('number', ['number', 'number'], 'dr_load'),
    poll: w('number', [], 'dr_poll_sound'),
  };
  // ---- M8 exports (task W3) -------------------------------------------------------
  // The screens bind these the same way (web/template.html bindApi); fail loudly if the wasm
  // build dropped one.
  const M8 = {
    newGame: ['null', ['number', 'number', 'number', 'number'], 'dr_new_game'],
    mode: ['number', [], 'dr_mode'],
    difficulty: ['number', [], 'dr_difficulty'],
    cls: ['number', ['number', 'number'], 'dr_class'],
    classSet: ['number', ['number', 'number', 'number', 'number'], 'dr_class_set'],
    unlockNext: ['number', ['number'], 'dr_unlock_next'],
    metaSave: ['number', [], 'dr_meta_save'],
    metaSaveLen: ['number', [], 'dr_meta_save_len'],
    metaLoad: ['number', ['number', 'number'], 'dr_meta_load'],
    skillActivate: ['number', ['number'], 'dr_skill_activate'],
    tap: ['number', ['number', 'number'], 'dr_tap'],
    preview: ['number', ['number', 'number'], 'dr_preview'],
    previewTile: ['number', ['number', 'number'], 'dr_preview_tile'],
    scoreAdd: ['number', [], 'dr_score_add'],
    score: ['number', ['number', 'number', 'number'], 'dr_score'],
    lifetime: ['number', ['number'], 'dr_lifetime'],
    clockEnabled: ['number', [], 'dr_clock_enabled'],   // W7 wall clock flag (Game+0x142f9)
    setClock: ['null', ['number'], 'dr_set_clock'],
  };
  const has = n => typeof M['_' + n] === 'function' || !!(M.asm && n in M.asm);
  for (const k of Object.keys(M8)) {
    const [ret, args, name] = M8[k];
    if (!has(name)) fail('missing wasm export ' + name + ' (W3 not wired into the build)');
    api[k] = w(ret, args, name);
  }

  const target = +process.argv[2] || 200;
  const rnd = xorshift(0xC0FFEE);
  let turns = 0, deaths = 0, menus = 0, invalid = 0, games = 0;

  function newGame(seed) {
    api.new(seed >>> 0, 1);
    while (api.poll() >= 0) {}   // drain start sounds
    games++;
  }

  // Random same-type walk (skulls and swords may link each other per CanMatchType).
  // Half the time start on a skull: killing attackers keeps the run alive and the XP
  // bar moving, which exercises the reward menus.
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
    const path = [{ c: c0, r: r0 }];
    const seen = new Set([c0 + r0 * 6]);
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

  function resolveMenu() {
    const kind = api.menu();
    if (kind < 0) return false;
    menus++;
    const rows = [];
    for (let i = 0; i < 4; i++) {
      const k = api.offer(i, 0);
      if (k < 0) break;
      rows.push({ i, kind: k, id: api.offer(i, 1), rank: api.offer(i, 2) });
    }
    if (!rows.length) fail('menu ' + kind + ' has no offers');
    if (kind === 0) {                              // level-up: two picks
      const skills = rows.filter(r => r.kind === 2);
      const a = skills.length ? skills[0].i : rows[0].i;
      const rest = rows.filter(r => r.i !== a);
      const b = rest.length ? (rest.find(r => r.kind === 2) || rest[0]).i : -1;
      api.choose(a, b);
    } else {                                       // shop / upgrade: first offer
      api.choose(rows[0].i, -1);
    }
    while (api.poll() >= 0) {}
    if (api.menu() >= 0) fail('menu still pending after choose');
    return true;
  }

  // ==== M8 / W3 exercises ==========================================================
  // Everything below is test-harness code (rules for what "accepted" means come from the M8
  // contract + the ClassScreen strings cited in src/m8.cpp), not game logic.
  let scoreTested = false;
  function testScoreInsert() {
    const rank = api.scoreAdd();
    if (rank < 0 || rank > 9) fail('dr_score_add returned ' + rank);
    const m = api.mode();
    const page = m === 0 ? 4 : m === 1 ? 5 : api.difficulty();   // ScoreType 0..5
    const sc = api.score(page, rank, 0);
    if (sc < 0) fail('dr_score(' + page + ',' + rank + ',0) empty right after insert');
    if (api.score(page, rank, 1) < 0) fail('dr_score class field empty');
    if (api.scoreAdd() !== -1) fail('second dr_score_add must be refused (one insert per run)');
    scoreTested = true;
    console.log('score insert ok (page ' + page + ', rank ' + rank + ', score ' + sc + ')');
  }

  // --- meta save / load round trip, incl. a patched class level so edits become possible ---
  {
    api.newGame(1, 1, 3, 0);                 // a live game is needed for the classes blob
    const mp = api.metaSave(), ml = api.metaSaveLen();
    // 4 magic + 4 len + 472 (2 B header + 10 x 47 B classes) + 4 len + 5 + 60*40 scores
    if (!mp || ml < 4 + 4 + 472 + 4 + 5 + 60 * 40) fail('dr_meta_save produced ' + ml + ' bytes');
    const meta = M.HEAPU8.slice(mp, mp + ml);
    if (meta[0] !== 0x44 || meta[1] !== 0x52) fail('meta container magic missing');
    const clsLen = meta[4] | (meta[5] << 8) | (meta[6] << 16) | (meta[7] << 24);
    if (clsLen !== 472) fail('meta classes payload = ' + clsLen + ' bytes, want 472');
    // Patch the all-10-classes blob (header 2 B + 10 x 47 B; level byte at record+26) so class 1
    // and class 8 reach level 10 — the only way a headless test can unlock class edits.
    const off = 8 + 2, REC = 47, LVL = 26;
    const patched = meta.slice(0);
    patched[off + 1 * REC + LVL] = 10;
    patched[off + 8 * REC + LVL] = 10;
    const lp = api.buf(ml);
    M.HEAPU8.set(patched, lp);
    if (!api.metaLoad(lp, ml)) fail('dr_meta_load rejected a valid blob');
    if (api.cls(1, 0) !== 10 || api.cls(8, 0) !== 10) fail('dr_meta_load did not apply the levels');
    if (api.cls(0, 0) !== 1) fail('Adventurer level must stay 1');
    if (api.cls(1, 12) !== 10) fail('dr_class replacement budget at lv10 = ' + api.cls(1, 12) + ', want 10');
    if (api.cls(1, 11) !== 4) fail('dr_class unlocked perk/flaw slots at lv10 = ' + api.cls(1, 11) + ', want 4');
    if (api.cls(1, 14) < 0 || api.cls(1, 33) < 0) fail('dr_class base-skill fields out of range');
    if (api.cls(1, 34) !== 255) fail('dr_class replacement slot 0 must start empty (255)');
    if (api.cls(1, 54) !== 0 || api.cls(1, 56) !== 0) fail('dr_class 54-58 counters must start at 0');
    if (api.cls(99, 0) !== -1 || api.cls(1, 99) !== -1) fail('dr_class must return -1 for bad args');
    // Reload the ORIGINAL blob so the rest of the run starts from a clean class state.
    const lp2 = api.buf(ml);
    M.HEAPU8.set(meta, lp2);
    if (!api.metaLoad(lp2, ml)) fail('dr_meta_load round trip failed');
    if (api.cls(1, 0) === 10) fail('dr_meta_load round trip kept the patched level');
    console.log('meta save/load round trip ok (' + ml + ' bytes)');
  }

  // --- class edits: refused vs accepted (gates = src/m8.cpp ClassSet) -----------------
  {
    api.newGame(7, 1, 3, 0);
    // refused: the Adventurer cannot be customised (ClassScreen str 0x5b56c)
    if (api.classSet(0, 0, 1, 0) !== 0) fail('Adventurer perk edit must be refused');
    if (api.classSet(0, 1, 1, 6) !== 0) fail('Adventurer flaw edit must be refused');
    if (api.classSet(0, 2, 0, 5) !== 0) fail('Adventurer race edit must be refused (str 0x5b840)');
    // refused: class 1 is locked (level 0) -> every slot gate fails
    if (api.classSet(1, 0, 1, 0) !== 0) fail('locked class perk edit must be refused');
    if (api.classSet(1, 3, 1, 0) !== 0) fail('locked class replacement edit must be refused');
    if (api.classSet(99, 0, 1, 0) !== 0) fail('bad klass must be refused');

    // unlock classes 1 and 8 through the meta blob, then the same edits become possible
    const mp = api.metaSave(), ml = api.metaSaveLen();
    const meta = M.HEAPU8.slice(mp, mp + ml);
    const off = 8 + 2, REC = 47, LVL = 26;
    const p = meta.slice(0);
    p[off + 1 * REC + LVL] = 10;
    p[off + 8 * REC + LVL] = 10;
    const lp = api.buf(ml);
    M.HEAPU8.set(p, lp);
    if (!api.metaLoad(lp, ml)) fail('metaLoad(unlocked classes) failed');
    api.newGame(8, 1, 3, 1);            // commit class 1 (SelectClass, FadeOut @0x3730c)

    if (api.stat(19) !== 1) fail('dr_new_game did not commit class 1 (activeClass=' + api.stat(19) + ')');
    if (api.classSet(1, 0, 1, 0) !== 1) fail('perk PERK_UNEXPECTED on an unlocked class must be accepted');
    if (api.cls(1, 4) !== 0) fail('perk slot 1 not stored');
    if (api.classSet(1, 0, 1, 0) !== 0) fail('duplicate perk must be refused');
    if (api.classSet(1, 0, 4, 0) !== 0) fail('perk slot 4 must be refused');
    if (api.classSet(1, 0, 1, 4) !== 0) fail('a locked class perk (Strong, class 4) must be refused');
    // FLAW_SLIM is class 1's INHERENT flaw (slot 0), so it is already a duplicate there —
    // use a free flaw for the accepted case (SetFlaw @0x35e6c only gates slot + duplicate).
    if (api.classSet(1, 1, 1, 7) !== 1) fail('flaw FLAW_CLUMSY on slot 1 must be accepted');
    if (api.cls(1, 8) !== 7) fail('flaw slot 1 not stored');
    if (api.classSet(1, 1, 1, 7) !== 0) fail('duplicate flaw must be refused');
    if (api.classSet(1, 1, 2, 6) !== 0) fail('duplicate of the inherent flaw must be refused');
    if (api.classSet(1, 1, 4, 7) !== 0) fail('flaw slot 4 must be refused');
    if (api.classSet(1, 2, 0, 2) !== 1) fail('race RACE_DWARF (class 8 at lv10) must be accepted');
    if (api.cls(1, 1) !== 2) fail('race not stored');
    if (api.classSet(1, 2, 0, 3) !== 0) fail('race RACE_SAUREN (class 6 locked) must be refused');
    if (api.classSet(1, 3, 1, 0) !== 1) fail('replacement skill on slot 1 must be accepted');
    if (api.cls(1, 35) !== 0) fail('replacement skill not stored');
    if (api.classSet(1, 3, 0, 0) !== 0) fail('replacement of slot 0 must be refused');
    if (api.classSet(1, 3, 2, 2) !== 0) fail('replacement by a BASE skill must be refused');
    console.log('class edits refused/accepted ok');

    // --- per-class games: every class x every mode -------------------------------------
    let checked = 0;
    for (let k = 0; k < 10; k++) {
      for (const m of [3, 0, 1]) {
        api.newGame(1000 + k * 8 + m, 2, m, k);
        if (api.mode() !== m) fail('dr_mode=' + api.mode() + ' want ' + m + ' (class ' + k + ')');
        if (api.difficulty() !== 2) fail('dr_difficulty=' + api.difficulty() + ' want 2');
        const want = (k === 0 || k === 1 || k === 8) ? k : 0;   // locked -> Adventurer
        if (api.stat(19) !== want)
          fail('class ' + k + ' committed as ' + api.stat(19) + ', want ' + want);
        if (api.over() !== 0) fail('fresh game already over');
        checked++;
      }
    }
    console.log('dr_new_game ok (' + checked + ' class/mode combinations)');
  }

  // --- skill activation, preview, lifetime stats -------------------------------------
  {
    api.newGame(4242, 1, 3, 0);
    // Skills are granted by level-up menus, so play until one is owned (resolveMenu prefers the
    // skill offers, kind 2) before exercising dr_skill_activate. Test-harness rule, not game logic.
    let nSkills = api.stat(21), guard = 0;
    while (nSkills === 0 && guard++ < 800) {
      if (api.over()) { newGame(4242 ^ guard); nSkills = api.stat(21); continue; }  // try again
      if (api.menu() >= 0) { resolveMenu(); nSkills = api.stat(21); continue; }
      const p = randomPath();
      if (!p) continue;
      const bytes = new Uint8Array(p.map(q => q.c + q.r * 6));
      const ptr = api.buf(bytes.length);
      M.HEAPU8.set(bytes, ptr);
      if (api.play(ptr, bytes.length)) nSkills = api.stat(21);
    }
    if (nSkills === 0) fail('no skill owned after ' + guard + ' attempts');
    let activated = 0, refused = 0;
    for (let s = 0; s < nSkills && s < 4; s++) {
      const ok = api.skillActivate(s);
      if (ok !== 0 && ok !== 1) fail('dr_skill_activate returned ' + ok);
      if (ok === 1) activated++; else refused++;
    }
    while (api.menu() >= 0) resolveMenu();
    if (activated === 0) fail('no skill activated (' + nSkills + ' owned)');
    console.log('skill activation ok (' + activated + ' fired, ' + refused + ' refused)');

    // preview: must not move the board, the hp/turn counters or any state the save sees
    const before = [];
    for (let c = 0; c < 6; c++) for (let r = 0; r < 6; r++) before.push(api.tile(c, r));
    const hp0 = api.stat(0), turn0 = api.stat(8), score0 = api.stat(10);
    let previews = 0;
    for (let tries = 0; tries < 40 && !previews; tries++) {
      const path = randomPath();
      if (!path) continue;
      const bytes = new Uint8Array(path.map(q => q.c + q.r * 6));
      const ptr = api.buf(bytes.length);
      M.HEAPU8.set(bytes, ptr);
      const atk = api.preview(ptr, bytes.length);
      if (typeof atk !== 'number' || atk < 0) fail('dr_preview returned ' + atk);
      for (const q of path) {
        const v = api.previewTile(q.c, q.r);
        if (typeof v !== 'number' || v < 0) fail('dr_preview_tile returned ' + v);
        if ((v & 3) > 3) fail('dr_preview_tile damage type out of range: ' + v);
        if (v > 65535) fail('dr_preview_tile packed value too wide: ' + v);
      }
      previews++;
    }
    if (!previews) fail('could not stage a preview chain');
    for (let c = 0; c < 6; c++) for (let r = 0; r < 6; r++)
      if (api.tile(c, r) !== before[c * 6 + r]) fail('preview changed the board at ' + c + ',' + r);
    if (api.stat(0) !== hp0 || api.stat(8) !== turn0 || api.stat(10) !== score0)
      fail('preview changed hp/turn/score');
    console.log('preview ok (1 chain, board + counters untouched)');

    for (let i = 0; i < 15; i++) {
      const v = api.lifetime(i);
      if (typeof v !== 'number' || !isFinite(v)) fail('dr_lifetime(' + i + ') = ' + v);
    }
    if (api.lifetime(15) !== 0) fail('dr_lifetime out of range must be 0');
    if (api.score(0, 0, 0) !== -1) fail('dr_score on an empty table must be -1');
    if (api.unlockNext(0) !== -1) fail('dr_unlock_next with no popup must be -1');
    if (api.unlockNext(1) !== -1) fail('dr_unlock_next level with no popup must be -1');
    // W7: the clock is a wall clock — engine keeps only the on/off flag (Game+0x142f9,
    // Game::SetClockState @0x25038); dr_stat 31 is removed.
    if (api.clockEnabled() !== 0) fail('clock must start off (java:121 default false)');
    api.setClock(1);
    if (api.clockEnabled() !== 1) fail('dr_set_clock(1) must set Game+0x142f9');
    api.setClock(0);
    if (api.clockEnabled() !== 0) fail('dr_set_clock(0) must clear Game+0x142f9');
    // dr_tap: Game::TapTile collects EXACTLY one trophy tile and refuses anything else
    // (turn.cpp:395). Only the refusal is exercised here, so the tap cannot perturb the run.
    const t0 = api.tile(0, 0);
    const isTrophy = (t0 & 7) === 6 && ((t0 >> 9) & 3) === 1;
    if (!isTrophy && api.tap(0, 0) !== 0) fail('dr_tap accepted a non-trophy tile');
    console.log('lifetime/unlock/empty-score/tap ok');
  }

  newGame(123456789);

  let snap = null;
  while (turns < target) {
    if (api.menu() >= 0) { resolveMenu(); continue; }
    if (api.over()) {
      deaths++;
      if (!scoreTested) testScoreInsert();   // W3: one ScoreBook insert per run
      if (turns >= target) break;
      newGame(0x1234 ^ turns ^ (deaths * 7919));
      continue;
    }
    let played = false;
    for (let tryN = 0; tryN < 40 && !played; tryN++) {
      const p = randomPath();
      if (!p) continue;
      const bytes = new Uint8Array(p.map(q => q.c + q.r * 6));
      const ptr = api.buf(bytes.length);
      M.HEAPU8.set(bytes, ptr);
      if (api.play(ptr, bytes.length)) {
        played = true;
        turns++;
        if (turns === 60) {                        // mid-run save round-trip
          const sp = api.save(), sn = api.saveLen();
          if (!sp || sn <= 16) fail('save produced ' + sn + ' bytes');
          const saved = M.HEAPU8.slice(sp, sp + sn);
          const turnBefore = api.stat(8), hpBefore = api.stat(0), scoreBefore = api.stat(10);
          // Save format (src/save.cpp) persists: type always; enemyType/enemy block only for
          // skulls; specialVariant only for specials; frozen/flaming/variant flags always.
          // inChain is transient draw state; stale enemyType on converted tiles is invisible
          // in game (only consulted for skulls) — mask both out, compare the rest.
          const persist = p => {
            const type = p & 7;
            let m = 0x7 | (((1 << 9) - 1) << 11);   // type + bits 11..19
            if (type === 1) m |= 63 << 3;
            if (type === 6) m |= 3 << 9;
            return p & m;
          };
          const boardBefore = [], enemyBefore = [];
          for (let c = 0; c < 6; c++) for (let r = 0; r < 6; r++) {
            boardBefore.push(persist(api.tile(c, r)));
            enemyBefore.push([0, 1, 2, 3, 4].map(f => api.enemy(c, r, f)));
          }
          const lp = api.buf(sn);
          M.HEAPU8.set(saved, lp);
          if (!api.load(lp, sn)) fail('load rejected a fresh save');
          if (api.stat(8) !== turnBefore || api.stat(0) !== hpBefore || api.stat(10) !== scoreBefore)
            fail('load mismatch: turn/hp/score ' + api.stat(8) + '/' + api.stat(0) + '/' + api.stat(10) +
                 ' vs ' + turnBefore + '/' + hpBefore + '/' + scoreBefore);
          for (let c = 0; c < 6; c++) for (let r = 0; r < 6; r++) {
            const i = c * 6 + r;
            if (persist(api.tile(c, r)) !== boardBefore[i]) fail('load board mismatch at ' + c + ',' + r);
            if ((api.tile(c, r) & 7) === 1) {      // skull stats are part of the save
              const e = [0, 1, 2, 3, 4].map(f => api.enemy(c, r, f));
              if (e.join() !== enemyBefore[i].join()) fail('load enemy mismatch at ' + c + ',' + r);
            }
          }
          snap = sn;
          console.log('save/load round-trip ok (' + sn + ' bytes)');
        }
      } else invalid++;
    }
    if (!played && api.menu() < 0 && !api.over()) {
      // no random path worked this tick — nudge with an exhaustive 3-tile probe
      let found = false;
      for (let i = 0; i < 36 && !found; i++) {
        const a = { c: i % 6, r: (i / 6) | 0 };
        for (let d = 0; d < 9 && !found; d++) {
          const b = { c: a.c + (d % 3) - 1, r: a.r + ((d / 3) | 0) - 1 };
          if (b.c < 0 || b.c > 5 || b.r < 0 || b.r > 5) continue;
          for (let e = 0; e < 9 && !found; e++) {
            const c = { c: b.c + (e % 3) - 1, r: b.r + ((e / 3) | 0) - 1 };
            if (c.c < 0 || c.c > 5 || c.r < 0 || c.r > 5) continue;
            const bytes = new Uint8Array([a.c + a.r * 6, b.c + b.r * 6, c.c + c.r * 6]);
            const ptr = api.buf(3);
            M.HEAPU8.set(bytes, ptr);
            if (api.play(ptr, 3)) { found = true; turns++; }
          }
        }
      }
      if (!found) { console.log('stuck at turn ' + api.stat(8) + ', restarting'); newGame(0xABCDEF ^ turns); }
    }
  }

  while (api.menu() >= 0) resolveMenu();
  if (!scoreTested) {
    // W3: no run died inside the sample — keep playing one game until CheckEndGame fires so the
    // score insert is still exercised (a classic game always ends: enemies attack every turn
    // after turn 4 and health never regenerates on its own). Test-harness code, not game logic.
    newGame(0xF00D);
    for (let n = 0; n < 4000 && !api.over(); n++) {
      if (api.menu() >= 0) { resolveMenu(); continue; }
      let played = false;
      for (let t = 0; t < 40 && !played; t++) {
        const p = randomPath();
        if (!p) continue;
        const bytes = new Uint8Array(p.map(q => q.c + q.r * 6));
        const ptr = api.buf(bytes.length);
        M.HEAPU8.set(bytes, ptr);
        played = !!api.play(ptr, bytes.length);
      }
      if (!played && api.menu() < 0) break;
    }
    while (api.menu() >= 0) resolveMenu();
    if (!api.over()) fail('game never reached game over in 4000 moves');
    testScoreInsert();
  }
  console.log('--- ' + turns + ' turns across ' + games + ' games, ' + deaths +
              ' deaths, ' + menus + ' menus, ' + invalid + ' rejected random paths' +
              (snap ? ', save ' + snap + 'B' : ''));
  if (turns < target) fail('only ' + turns + ' turns played');
  console.log('PASS');
})().catch(e => fail(e && e.stack || String(e)));
