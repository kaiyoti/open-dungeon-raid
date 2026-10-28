// S2 fix #1 check: the page's tile-press "start" sound map must equal Game::QueueTileSound
// @00029800 (tile+0xa0 -> id) and every id must index SOUND_FILES to a real <base>1.ogg.
// Extracts the ACTUAL tileStartId/tileStartSkip functions from template.html and runs them with a
// stubbed api.tile. Test harness only (AGENTS.md): it checks the page mapping, not game logic.
'use strict';
const fs = require('fs'), path = require('path');
const tpl = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
const soundsDir = path.resolve(__dirname, '..', '..', 'assets', 'ripped', 'sounds');
const files = new Set(fs.readdirSync(soundsDir));

let fails = 0;
const bad = m => { console.error('FAIL: ' + m); fails++; };

// pull the three functions out of the page verbatim
const m = tpl.match(/function tileStartId\([\s\S]*?\n\}\nfunction playSoundId\([\s\S]*?\n\}\n/);
if (!m) { bad('tileStartId/playSoundId block not found in template.html'); process.exit(1); }
const T_POTION = 0, T_SKULL = 1, T_COIN = 2, T_SHIELD = 3, T_SWORD = 4, T_SPECIAL = 6;
const SOUND_FILES = ['click','coinstart','coin','swordstart','potion','potionstart','shield',
  'shieldstart','up','xp','abilityready','shop','levelup','upgrade','skull','armour','armourbreak',
  'damage','start','ability','freeze','teleport','shatter','boost','explosive','slash','attackmiss',
  'attackhit','explosion','poison','blunt','help','heartbeat','lose','doorcreak','jump','bless','quake'];
const api = { tile: () => 0 };
// build the page functions with api + T_ constants injected (playSoundId defined but unused here)
const factory = new Function('api', 'T_POTION', 'T_SKULL', 'T_COIN', 'T_SHIELD', 'T_SWORD', 'T_SPECIAL',
  m[0] + '\nreturn { tileStartId, tileStartSkip };');
const { tileStartId, tileStartSkip } = factory(api, T_POTION, T_SKULL, T_COIN, T_SHIELD, T_SWORD, T_SPECIAL);

// pack a tile the way dr_tile does: bits 0-2 type, 13 mana, 14 acid, 15 explosive, 17 arrow
const pack = (type, f = {}) => type | (f.acid ? 1 << 14 : 0) | (f.explosive ? 1 << 15 : 0) | (f.arrow ? 1 << 17 : 0);
const setTile = packed => { api.tile = () => packed; };

// QueueTileSound @00029800 expected id per (type, flags)
const cases = [
  [pack(T_POTION), 5, 'potionstart'],                 // plain potion -> 5
  [pack(T_POTION, { mana: 1 }), 5, 'potionstart'],    // mana(+0x242) not checked -> 5
  [pack(T_POTION, { acid: 1 }), 24, 'explosive'],     // acid(+0x243) -> 24
  [pack(T_POTION, { explosive: 1 }), 24, 'explosive'],// explosive(+0x244) -> 24
  [pack(T_SKULL), 14, 'skull'],
  [pack(T_COIN), 1, 'coinstart'],
  [pack(T_SHIELD), 7, 'shieldstart'],
  [pack(T_SWORD), 3, 'swordstart'],
  [pack(T_SPECIAL), 8, 'up'],
  [pack(5), -1, null],                                // type 5: no QueueTileSound case
];
for (const [packed, wantId, wantBase] of cases) {
  setTile(packed);
  const id = tileStartId(0, 0);
  if (id !== wantId) bad(`tileStartId(type=${packed & 7},acid=${(packed>>14)&1},expl=${(packed>>15)&1}) = ${id}, want ${wantId}`);
  else if (wantBase && SOUND_FILES[id] !== wantBase) bad(`SOUND_FILES[${id}] = ${SOUND_FILES[id]}, want ${wantBase}`);
  else if (wantBase && !files.has(wantBase + '1.ogg')) bad(`missing file ${wantBase}1.ogg`);
}

// arrow-sword skip (ProcessTileSelection @0002c8dc:216-218): head sword+arrow + new not sword/skull
if (!tileStartSkip(pack(T_SWORD, { arrow: 1 }), T_COIN)) bad('skip should be TRUE for arrow-sword -> coin');
if (!tileStartSkip(pack(T_SWORD, { arrow: 1 }), T_SHIELD)) bad('skip should be TRUE for arrow-sword -> shield');
if (tileStartSkip(pack(T_SWORD, { arrow: 1 }), T_SWORD)) bad('skip must be FALSE for arrow-sword -> sword');
if (tileStartSkip(pack(T_SWORD, { arrow: 1 }), T_SKULL)) bad('skip must be FALSE for arrow-sword -> skull');
if (tileStartSkip(pack(T_SWORD), T_COIN)) bad('skip must be FALSE for plain (non-arrow) sword');
if (tileStartSkip(pack(T_SHIELD), T_COIN)) bad('skip must be FALSE for non-sword head');

console.log(fails ? fails + ' failures' : 'PASS: tile-press start-sound map == QueueTileSound @00029800');
process.exit(fails ? 1 : 0);
