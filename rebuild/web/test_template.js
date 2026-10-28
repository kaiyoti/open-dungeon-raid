// W4b node test: syntax-check the app script inside rebuild/web/template.html
// (the block after the inlined dr.js). Catches typos without a browser.
// Test harness code only — not game logic.
'use strict';
const fs = require('fs');
const path = require('path');
const html = fs.readFileSync(path.join(__dirname, 'template.html'), 'utf8');
const blocks = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
if (blocks.length < 2) fail('expected 2 <script> blocks (dr.js + app), got ' + blocks.length);
const app = blocks[blocks.length - 1];
if (!app.includes("'use strict'")) fail('app script missing use strict');
// U12: __RECREATED__ marker removed with the Recreated theme (build.sh no longer embeds it).
for (const marker of ['__DR_JS__', '__ART__', '__ASSET_BASE__']) {
  if (!html.includes(marker)) fail('template missing marker ' + marker);
}
try {
  new Function(app.replace(/__ART__/g, 'false'));   // parse only, not executed
} catch (e) {
  fail('app script syntax error: ' + e.message);
}
// crude balance check of the html tags we add
for (const id of ['scrClasses', 'clsTabs', 'clsBody', 'clsHelp', 'unlockOverlay', 'ukTitle', 'ukBody', 'ukOk',
                  'skillBar', 'hudExtra', 'helpBanner']) {
  if (!html.includes('id="' + id + '"')) fail('missing element id "' + id + '"');
}
// W5: every W3 export the screens call must be bound directly (no JS stubs left — W3 landed,
// tasks/W3/RESULT.md) and no "STUB until W3" path may remain.
for (const n of ['dr_new_game', 'dr_mode', 'dr_difficulty', 'dr_class', 'dr_class_set',
                 'dr_unlock_next', 'dr_meta_save', 'dr_meta_load', 'dr_skill_activate', 'dr_tap',
                 'dr_preview', 'dr_preview_tile', 'dr_score_add', 'dr_score', 'dr_lifetime',
                 'dr_label_count', 'dr_label', 'dr_label_clear']) {
  if (!app.includes("'" + n + "'")) fail('app script missing export binding ' + n);
}
if (app.includes('STUB until W3')) fail('app script still contains a STUB-until-W3 path');
console.log('PASS: template.html app script parses, ' + app.split('\n').length + ' lines');
function fail(msg) { console.error('FAIL: ' + msg); process.exit(1); }
