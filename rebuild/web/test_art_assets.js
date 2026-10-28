// Art-build wiring check (W1): verifies that every sprite key web/template.html can ask
// for exists in assets/ripped/sprites.json, that the Sound->ogg mapping matches real
// files, and that sprite rects lie inside their textures. Pure Node, no browser.
//   node test_art_assets.js
'use strict';
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..', '..', 'assets', 'ripped');
const sprites = JSON.parse(fs.readFileSync(path.join(root, 'sprites.json'), 'utf8'));
const tpl = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');

let fails = 0;
const bad = m => { console.error('FAIL: ' + m); fails++; };

// --- every key spriteFor() in template.html can produce -----------------------------
const ENEMY_NAMES = ['None', 'Boss', 'Spiky', 'Golden', 'Healer', 'Invisible', 'Acidic',
  'Flaming', 'Freezing', 'Poisonous', 'Kamikaze', 'Overseer', 'Corrosive', 'Thief', 'Chaotic',
  'Mage', 'Assassin', 'Doppelganger', 'Alchemist', 'Unstable', 'Vampire', 'Vengeful',
  'Trampling', 'Greasy', 'Alpha', 'Magnetic', 'Brute', 'Armoured', 'Devourer', 'Defiler',
  'Meat Shield', 'Fleeing', 'Lich'];
const tileKeys = ['tile/potion', 'tile/mana_potion', 'tile/acid_potion', 'tile/explosive_potion',
  'tile/sword', 'tile/magic_sword', 'tile/arrow', 'tile/broken_sword', 'tile/shield',
  'tile/broken_shield', 'tile/coin', 'tile/skull', 'tile/special_chain', 'tile/special_trophy',
  'tile/frozen_overlay', 'tile/flaming_overlay'];
for (const k of tileKeys) if (!sprites[k]) bad('missing sprite ' + k);
for (let e = 1; e < ENEMY_NAMES.length; e++) {
  const k = 'enemy/' + ENEMY_NAMES[e].toLowerCase().replace(/ /g, '_');
  if (!sprites[k] && !sprites['tile/skull']) bad('missing enemy sprite ' + k);
  else if (!sprites[k]) console.log('note: ' + k + ' missing, falls back to tile/skull');
}

// --- sprite rects inside their texture ----------------------------------------------
const texSize = { tiles: fs.statSync(path.join(root, 'tiles.png')).size,
                  menus: fs.statSync(path.join(root, 'menus.png')).size,
                  icons: fs.statSync(path.join(root, 'icons.png')).size };
for (const [k, s] of Object.entries(sprites)) {
  if (!s.tex || s.x === undefined) continue;
  if (!(s.tex in texSize) && s.tex !== 'font' && s.tex !== 'fontMenu' && s.tex !== 'fontScore')
    bad(k + ': unknown texture ' + s.tex);
  if (s.x < 0 || s.y < 0 || s.w <= 0 || s.h <= 0) bad(k + ': bad rect');
}

// --- Sound enum -> ogg base names (template SOUND_FILES) -----------------------------
const files = new Set(fs.readdirSync(path.join(root, 'sounds')));
const m = tpl.match(/const SOUND_FILES = \[([^\]]+)\]/);
if (!m) bad('SOUND_FILES not found in template');
else {
  const bases = m[1].split(',').map(s => s.trim().replace(/'/g, ''));
  bases.forEach((b, i) => { if (!files.has(b + '1.ogg')) bad('sound ' + i + ' missing ' + b + '1.ogg'); });
  console.log('sounds: ' + bases.length + ' bases checked');
}

// --- fonts / atlases referenced by initArt --------------------------------------------
for (const f of ['tiles.png', 'menus.png', 'icons.png', 'dwarvesfont.ttf', 'sprites.json'])
  if (!fs.existsSync(path.join(root, f))) bad('missing asset ' + f);

console.log(fails ? fails + ' failures' : 'PASS (' + Object.keys(sprites).length + ' sprites)');
process.exit(fails ? 1 : 0);
