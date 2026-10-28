// TEST HARNESS code (not game logic): checks the strict canvas stub itself.
'use strict';
const assert = require('assert');
const { makeStrictCtx } = require('./canvas_stub');

let failures = 0;
function check(name, fn) {
  try { fn(); console.log('PASS', name); }
  catch (e) { failures++; console.log('FAIL', name, '-', e.message); }
}

check('arcTo with 4 args throws', () => {
  const ctx = makeStrictCtx();
  assert.throws(() => ctx.arcTo(1, 2, 3, 4), /ctx\.arcTo: needs 5 args, got 4/);
});
check('arcTo with 5 args does not throw', () => {
  const ctx = makeStrictCtx();
  ctx.arcTo(1, 2, 3, 4, 5);
});
check('fillText with 2 args throws', () => {
  const ctx = makeStrictCtx();
  assert.throws(() => ctx.fillText('hi', 10), /ctx\.fillText: needs 3 args, got 2/);
});
check('fillText with 3 args does not throw', () => {
  const ctx = makeStrictCtx();
  ctx.fillText('hi', 10, 20);
});
check('plain fields store and return', () => {
  const ctx = makeStrictCtx();
  ctx.fillStyle = '#fff';
  assert.strictEqual(ctx.fillStyle, '#fff');
});
check('unknown method throws', () => {
  const ctx = makeStrictCtx();
  assert.throws(() => ctx.filterBlur, /ctx\.filterBlur: not in the strict stub/);
});

process.exit(failures ? 1 : 0);
