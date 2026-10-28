// TEST HARNESS code (not game logic): strict canvas 2D context stub for the
// node tests. A plain no-op Proxy let template.html's ctx.arcTo(4 args) bug
// pass every test; this stub throws on wrong argument counts so such bugs fail.

'use strict';

const FIELDS = new Set([
  'fillStyle', 'strokeStyle', 'globalAlpha', 'font', 'lineWidth', 'textAlign',
  'textBaseline', 'globalCompositeOperation', 'lineCap', 'lineJoin',
  'shadowBlur', 'shadowColor', 'imageSmoothingEnabled', 'canvas',
]);

// name -> minimum arg count, or array of allowed exact counts
const METHODS = {
  arc: 5, arcTo: 5, bezierCurveTo: 6, quadraticCurveTo: 4, ellipse: 7,
  moveTo: 2, lineTo: 2, rect: 4, roundRect: 4,
  fillRect: 4, strokeRect: 4, clearRect: 4,
  fillText: 3, strokeText: 3,
  drawImage: [3, 5, 9],
  translate: 2, scale: 2, rotate: 1, transform: 6,
  setTransform: [0, 6],
  createLinearGradient: 4, createRadialGradient: 6,
  createPattern: 2, getImageData: 4, putImageData: 3,
  measureText: 1, setLineDash: 1,
  save: 0, restore: 0, beginPath: 0, closePath: 0,
  fill: 0, stroke: 0, clip: 0, resetTransform: 0,
};

function makeGradient() {
  return {
    addColorStop(offset, color) {
      if (arguments.length < 2) throw Error('ctx.gradient.addColorStop: needs 2 args, got ' + arguments.length);
    },
  };
}

function makeStrictCtx() {
  const store = {};
  const fns = new Map();

  function makeFn(name, spec) {
    const fn = function () {
      const n = arguments.length;
      if (Array.isArray(spec)) {
        if (!spec.includes(n)) {
          throw Error('ctx.' + name + ': needs ' + spec.join(' or ') + ' args, got ' + n);
        }
      } else if (n < spec) {
        throw Error('ctx.' + name + ': needs ' + spec + ' args, got ' + n);
      }
      if (name === 'measureText') return { width: 0 };
      if (name === 'createLinearGradient' || name === 'createRadialGradient') return makeGradient();
      if (name === 'getImageData') {
        const w = arguments[2], h = arguments[3];
        return { data: new Uint8ClampedArray(w * h * 4), width: w, height: h };
      }
      return undefined;
    };
    return fn;
  }

  return new Proxy({}, {
    get(target, prop) {
      if (typeof prop === 'symbol') return undefined;
      if (FIELDS.has(prop)) return store[prop];
      if (Object.prototype.hasOwnProperty.call(METHODS, prop)) {
        if (!fns.has(prop)) fns.set(prop, makeFn(prop, METHODS[prop]));
        return fns.get(prop);
      }
      throw Error('ctx.' + prop + ': not in the strict stub');
    },
    set(target, prop, value) {
      if (typeof prop === 'symbol') { target[prop] = value; return true; }
      store[prop] = value;
      return true;
    },
    has(target, prop) {
      if (typeof prop === 'symbol') return Reflect.has(target, prop);
      return FIELDS.has(prop) || Object.prototype.hasOwnProperty.call(METHODS, prop);
    },
  });
}

module.exports = { makeStrictCtx };
