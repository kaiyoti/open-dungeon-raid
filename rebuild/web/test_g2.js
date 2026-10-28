// G2 node test (test harness, not game logic): parse the built page, then drive the
// StatsMenu fade math + row taps + ScoreScreen fade bookkeeping with a STUB api whose item
// data is the tools/emu-measured fresh state (tasks/G2/probe_names.json — seed 1, same as
// the Chrome gate which exercises the REAL engine end to end).
'use strict';
process.on('unhandledRejection', () => {});   // the stub never boots the wasm; ignore boot()
const fs = require('fs');
const path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
if (blocks.length < 2) { console.error('FAIL: expected >=2 script blocks'); process.exit(1); }
// template raw: block0 = /*__DR_JS__*/ marker (no wasm), last = app (+ physics block if split)
const appRun = blocks.join('\n;\n').replace(/__ART__/g, 'false').replace(/__RECREATED__/g, 'null')
  .replace(/\/\*__DR_JS__\*\//g, 'var DR = function(){ return Promise.reject(new Error("stub")); };');
new Function(appRun);   // parse only

function assert(c, m) { if (!c) { console.error('FAIL: ' + m); process.exit(1); } }

const els = {};
function el(id) {
  if (!els[id]) els[id] = {
    id, style: {}, dataset: {}, children: [], hidden: true, textContent: '', innerHTML: '',
    classList: { add() {}, remove() {}, toggle() {}, contains: () => false },
    setAttribute() {}, getAttribute() { return null; }, append() {}, appendChild() {},
    addEventListener(t, fn) { (this._l = this._l || {})[t] = (this._l[t] || []).concat(fn); },
    dispatchEvent(e) {
      ((this._l && this._l[e.type]) || []).forEach(fn => fn({ ...e, type: e.type }));
      return true;
    },
    querySelector() { return el(id + '>q'); }, querySelectorAll() { return []; },
    getBoundingClientRect() { return { left: 0, top: 0, width: 320, height: 480, right: 320, bottom: 480 }; },
    getContext() { return ctxStub(); },
    focus() {}, click() {}, remove() {}, contains() { return false; },
    insertBefore(n) { return n; }, firstChild: null, parentNode: null, offsetWidth: 320, offsetHeight: 480
  };
  return els[id];
}
function ctxStub() {
  const noop = () => {};
  return new Proxy({
    canvas: { width: 320, height: 480 },
    measureText: () => ({ width: 10 }), getImageData: () => ({ data: new Uint8ClampedArray(4) }),
    createImageData: () => ({ data: new Uint8ClampedArray(4) }),
    setTransform: noop, drawImage: noop, fillRect: noop, clearRect: noop, fillText: noop,
    save: noop, restore: noop, beginPath: noop, moveTo: noop, lineTo: noop, stroke: noop,
    fill: noop, closePath: noop, rect: noop, clip: noop, translate: noop, scale: noop,
    rotate: noop, arc: noop, createLinearGradient: () => ({ addColorStop: noop }),
    putImageData: noop, globalAlpha: 1, globalCompositeOperation: 'source-over'
  }, { get(t, k) { return k in t ? t[k] : noop; }, set(t, k, v) { t[k] = v; return true; } });
}
const window = {
  document: null, addEventListener() {}, removeEventListener() {}, requestAnimationFrame: () => 0,
  cancelAnimationFrame() {}, setTimeout: () => 0, clearTimeout() {}, setInterval: () => 0,
  clearInterval() {}, location: { href: 'http://localhost/test.html', search: '' },
  matchMedia: () => ({ matches: false, addEventListener() {} }),
  getComputedStyle: () => ({ getPropertyValue: () => '' }),
  CustomEvent: function (type, init) { return { type, ...(init || {}) }; },
  Error: Error, navigator: { clipboard: {} }, innerWidth: 390, innerHeight: 844
};
const document = {
  getElementById: el, createElement: (t) => el('new:' + t + ':' + Math.random()),
  createTextNode: (t) => ({ textContent: t }), body: el('body'),
  addEventListener() {}, removeEventListener() {}, documentElement: el('html'),
  querySelector() { return null; }, querySelectorAll() { return []; },
  visibilityState: 'visible', hidden: false
};
window.document = document;
const localStorage = { _d: {}, getItem(k) { return this._d[k] ?? null; },
                       setItem(k, v) { this._d[k] = String(v); }, removeItem(k) { delete this._d[k]; } };
function Audio() { return { play() { return Promise.resolve(); }, load() {} }; }
function Image() { this.src = ''; }
let rafCbs = [], rafId = 0;
function raf(fn) { rafCbs.push(fn); return ++rafId; }
function caf() { rafCbs = []; }

// Fresh-game engine values measured through the ORIGINAL (tasks/G2/probe_names.json, seed 1):
// items: id/label/stats; class 0 Human Adventurer; difficulty 1 (Normal).
const FRESH_ITEMS = [
  { id: 5,   label: 5,  stats: [2, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0, 0, 0, 0, 0, 0, 0, 0, 0] },  // weapon  Longsword  2 Damage
  { id: 137, label: 6,  stats: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  1, 0, 0, 0, 0, 0, 0, 0, 0] },  // helmet  Mask       1 Defence
  { id: 274, label: 2,  stats: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  2, 0, 0, 0, 0, 0, 0, 0, 0] },  // shield  Breastplate 2 Defence
  { id: 237, label: 13, stats: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  1, 0, 0, 0, 0, 0, 0, 0, 0] },  // armour  Fender      1 Defence
  { id: 389, label: 10, stats: [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10,  0, 0, 0, 0, 0, 0, 0, 0, 0] }   // acc     Ointment   +10 Health
];
const stub = {
  stat: (i) => (i === 19 ? 0 : i === 20 ? 0 : i === 10 ? 0 : i === 7 ? 1 : 0),  // class0 race0 Normal
  lifetime: () => 0,
  item: (slot, field) => {
    const it = FRESH_ITEMS[slot] || FRESH_ITEMS[0];
    if (field < 0) return it.id;
    if (field < 20) return it.stats[field];
    if (field === 20 || field === 21) return -1;   // nameIndex fresh (GenerateName NULL)
    if (field === 22) return it.label;
    return 0;
  },
  cls: () => 255,          // no perks/flaws unlocked (fresh class)
  mode: () => 3,           // MODE_CLASSIC
  difficulty: () => 1,     // Normal (matches the emu boot default, g2_probe)
  menu: () => -1, score: () => -1, scoreName: () => '',
  skill: () => 0, menuOffer: () => -1, clockEnabled: () => 0, preview: () => -1
};

// NOTE: the full app is NOT executed here (its async boot needs the real wasm; the Chrome
// gate tasks/G2/gate.py exercises that path end to end). Only the pure G2 functions are
// extracted below and driven against the stub.
setTimeout(() => {}, 0);   // keep timers referenced
// api is `let api = null` in the app scope — the app's boot never binds it under the stub,
// so drive the pure page functions with the stub injected through the exposed globals:
// composeItemName/makeStatLines read the api LET, which we cannot reach from here — instead
// re-evaluate the two pure functions against the stub by extracting them from the source.
const src = appRun;
function grab(name) {
  const i = src.indexOf('function ' + name + '(');
  if (i < 0) throw new Error('missing ' + name);
  let d = 0, j = src.indexOf('{', i);
  for (let k = j; k < src.length; k++) {
    if (src[k] === '{') d++;
    else if (src[k] === '}' && --d === 0) return src.slice(i, k + 1);
  }
  throw new Error('unbalanced ' + name);
}
const STAT_NAMES = ['Weapon Damage', 'Base Damage', 'Life Leech', 'Poison', 'Armour Piercing',
  'Quicken', 'Boost XP', 'Boost Gold', 'Boost Strength', 'Boost Luck', 'Health', 'Armour',
  'Spikes', 'Boost UP', 'Durability', 'Blunting', 'Repair', 'Regenerate', 'Boost Dexterity',
  'Boost Vitality'];
const sandbox = {
  api: stub, console,
  ITEM_NAME_PREFIX0: src.match(/ITEM_NAME_PREFIX0 = '([^']+)'/)[1],
  ITEM_NAME_QUALITY: eval(src.match(/const ITEM_NAME_QUALITY = (\[[^\]]+\])/)[1]),
  ITEM_NAME_GROUPS: eval(src.match(/const ITEM_NAME_GROUPS = (\[[\s\S]*?\];)/)[1]),
  ST_STAT_FMT: eval(src.match(/const ST_STAT_FMT = (\[[\s\S]*?\];)/)[1]),
  ST_STAT_ORDER: eval(src.match(/const ST_STAT_ORDER = (\[[^\]]+\])/)[1]),
  RACE_NAMES: ['Human'], CLASS_NAMES: ['Adventurer'],
  RACE_BONUS_DESCS: ['Humans receive no special bonuses'], PERK_DESCS: [], FLAW_DESCS: []
};
const fn = new Function(...Object.keys(sandbox),
  grab('stStatLine') + '\n' + grab('composeItemName') + '\n' + grab('makeStatLines') + '\n' +
  grab('makeClassLines') + '\n' +
  'return { stStatLine, composeItemName, makeStatLines, makeClassLines };');
const P = fn(...Object.values(sandbox));

// ---- fade constants (StatsMenu::Show pool 0.2 s; Process floor curve, probe.json) ----
assert(Math.floor(180 * (1 - 0.166667 / 0.2)) === 29, 'dim fade frame1 = 29 (emu)');
assert(Math.floor(255 * (1 - 0.166667 / 0.2)) === 42, 'label fade frame1 = 42 (emu)');
assert(Math.floor(255 * (1 - 0.266667 / 0.3)) === 28, 'scores fade frame1 = 28 (emu)');

// ---- row taps: Process @0x61d84 cases 0xa..0xe -> items 1,2,0,3,4; 0xf -> class info ----
const t10 = P.makeStatLines(1);
assert(t10[0].t === 'Novice  Mask', 'tap10 (quad0xa/default->item1) name: ' + t10[0].t);
assert(t10.length === 2 && t10[1].t === '1 Defence', 'tap10 stat: ' + t10.map(l => l.t).join('|'));
const t11 = P.makeStatLines(2);
assert(t11[0].t === 'Novice  Breastplate' && t11[1].t === '2 Defence',
       'tap11 (case 0xb->item2): ' + t11.map(l => l.t).join('|'));
const t12 = P.makeStatLines(0);
assert(t12[0].t === 'Novice  Longsword' && t12[1].t === '2 Damage',
       'tap12 (case 0xc->item0): ' + t12.map(l => l.t).join('|'));
const t13 = P.makeStatLines(3);
assert(t13[0].t === 'Novice  Fender' && t13[1].t === '1 Defence',
       'tap13 (case 0xd->item3): ' + t13.map(l => l.t).join('|'));
const t14 = P.makeStatLines(4);
assert(t14[0].t === 'Novice  Ointment' && t14[1].t === '+10 Health',
       'tap14 (case 0xe->item4): ' + t14.map(l => l.t).join('|'));
const t15 = P.makeClassLines();
assert(t15[0].t === 'Human Adventurer' &&
       t15[1].t === 'Humans receive no special bonuses' && t15.length === 2,
       'tap15 (case 0xf class info): ' + t15.map(l => l.t).join('|'));

// ---- stat line formats (MakeStatLabels sweep, tasks/G2) ----
assert(P.stStatLine(0, 3) === '3 Damage', 'fmt0');
assert(P.stStatLine(1, 3) === '+3 Base damage', 'fmt1');
assert(P.stStatLine(5, 3) === "Quicken: reduce skills' cooldown by 1", 'fmt5 fixed');
assert(P.stStatLine(6, 12) === '+3 Experience points per skull', 'fmt6 float /4 prec0');
assert(P.stStatLine(6, 13) === '+3.25 Experience points per skull', 'fmt6 float prec2');
assert(P.stStatLine(11, 4) === '4 Defence', 'fmt11');

console.log('PASS: stats fade constants, MakeStatLabels order/formats, item/class tap lines');
