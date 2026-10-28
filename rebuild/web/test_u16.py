#!/usr/bin/env python3
# U16 REGRESSION TEST (TEST HARNESS code only — not game logic):
# "the web version tile swiping is broken" (user 2026-10-28). Root cause:
# the canvas pointerdown CONSUMED presses inside the help-bubble box (F5 port), so every
# swipe that started under a bubble did nothing while the tip queue kept feeding new bubbles.
# The original does not consume: Game::Process @0x339ac runs Game::ProcessInput (:84) AND
# HelpMenu::Process (:519, box test -> FadeOut) on the same polled touch in the same frame.
# This test drags a chain with REAL pointer input (mouse, desktop 1280x800) AND REAL touch
# input (CDP Input.dispatchTouchEvent, mobile 390x844 dpr3) and asserts the turn advances —
# including a chain that STARTS UNDER THE LIVE N0 BUBBLE (the eaten-tap regression guard).
# ONE headless Chrome host-wide (free -m >= 4000 gate). Fails pre-fix, passes post-fix.
import asyncio, base64, functools, http.server, json, os, socket, subprocess, sys, tempfile, threading, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SHOTS = os.path.join(ROOT, 'tasks', 'U16', 'shots')
CHROME = os.environ.get('CHROME', '/usr/bin/google-chrome')
if not os.path.exists(CHROME):
    CHROME = '/usr/bin/chromium'
SEED = 0x5516   # fixed run: deterministic board (dr_new_game reseeds everything, W6)

def free_port():
    s = socket.socket(); s.bind(('127.0.0.1', 0)); p = s.getsockname()[1]; s.close(); return p

class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a): pass

def mem_available_mb():
    for line in subprocess.run(['free', '-m'], capture_output=True, text=True).stdout.splitlines():
        f = line.split()
        if f and f[0].rstrip(':') == 'Mem' and len(f) >= 7:
            return int(f[6])
    return -1

def mem_guard(limit=4000, max_wait=600):
    t0 = time.time()
    while True:
        a = mem_available_mb()
        if a >= limit: return a
        if time.time() - t0 > max_wait:
            raise RuntimeError('host memory: available %d MB < %d MB after %ds' % (a, limit, max_wait))
        print('mem: available %d MB < %d, waiting...' % (a, limit)); sys.stdout.flush()
        time.sleep(15)

class CDP:
    def __init__(self, ws): self.ws, self.id, self.events = ws, 0, []
    async def call(self, method, **params):
        self.id += 1; mid = self.id
        await self.ws.send(json.dumps({'id': mid, 'method': method, 'params': params}))
        while True:
            msg = json.loads(await self.ws.recv())
            m = msg.get('method')
            if m == 'Runtime.consoleAPICalled' and msg['params'].get('type') == 'error':
                self.events.append('console: ' + ' '.join(
                    a.get('value', a.get('description', ''))
                    for a in msg['params'].get('args', []))[:300])
                continue
            if m == 'Runtime.exceptionThrown':
                self.events.append('exc: ' + str(msg['params'].get('exceptionDetails', {})
                                                .get('exception', {}).get('description', ''))[:300])
                continue
            if msg.get('id') == mid:
                if 'error' in msg: raise RuntimeError(msg['error'])
                return msg.get('result', {})

async def ev(cdp, expr):
    r = await cdp.call('Runtime.evaluate', expression=expr, returnByValue=True, awaitPromise=True)
    if 'exceptionDetails' in r:
        return 'EXC:' + str(r['exceptionDetails'].get('exception', {}).get('description', ''))[:300]
    return r.get('result', {}).get('value')

async def wait_for(cdp, expr, timeout=30):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if await ev(cdp, '(function(){try{return (%s)}catch(e){return false}})()' % expr) is True:
            return True
        await asyncio.sleep(0.2)
    return False

HELPERS = r"""
window.__U16T = {
  state: function(){
    var h = 0;
    for (var r=0;r<6;r++) for (var c=0;c<6;c++) h = (h*31 + api.tile(c,r)) >>> 0;
    return JSON.stringify({turn: api.stat(8), over: api.over(), busy: busy(), hash: h,
      tip: (typeof helpDraw!=='undefined'&&helpDraw) ? helpDraw.n : -1});
  },
  cellPt: function(c,r){
    var rect = canvas.getBoundingClientRect();
    var cellPx = rect.width * FX.CELL;
    return JSON.stringify([rect.left + ((c+0.5) + FX.BOARD_LEFT/FX.CELL) * cellPx,
                           rect.top + boardDy + (r+0.5) * cellPx]);
  },
  walk: function(sc, sr){   // greedy 3+ chain from (sc,sr) using the page's own pathOk
    function ok(cells){ var b = new Uint8Array(cells.map(function(q){return q.c+q.r*6;}));
      var p = api.buf(b.length); M.HEAPU8.set(b,p); return api.pathOk(p,b.length)===1; }
    var path = [{c:sc,r:sr}];
    if (!ok([path[0]])) return 'null';
    for (var s=0;s<5;s++){ var head=path[path.length-1], added=false;
      for (var dr=-1;dr<=1&&!added;dr++) for (var dc=-1;dc<=1&&!added;dc++){
        if(!dr&&!dc) continue;
        var nc=head.c+dc, nr=head.r+dr;
        if(nc<0||nc>5||nr<0||nr>5) continue;
        if(path.some(function(q){return q.c===nc&&q.r===nr;})) continue;
        if(ok(path.concat([{c:nc,r:nr}]))){path.push({c:nc,r:nr});added=true;}
      } }
    return path.length>=3 ? JSON.stringify(path) : 'null';
  },
  findChain: function(){
    for (var r=0;r<6;r++) for (var c=0;c<6;c++) {
      var p = window.__U16T.walk(c,r);
      if (p !== 'null') return p;
    }
    return 'null';
  },
  chainUnderTip: function(){   // a 3+ chain whose START cell centre lies in the tip box
    if (!helpDraw) return 'null';
    var L = helpLayout(helpDraw.n, helpDraw.ax, helpDraw.ay);
    for (var r=0;r<6;r++) for (var c=0;c<6;c++) {
      var x = FX.BOARD_LEFT + (c+0.5)*FX.CELL, y = FX.center(r);
      if (x >= L.tx && x < L.tx+0.98 && y >= L.ty && y < L.ty+L.H) {
        var p = window.__U16T.walk(c,r);
        if (p !== 'null') return p;
      }
    }
    return 'null';
  }
};
"""

async def drag(cdp, mode, path):
    pts = []
    for q in path:
        v = json.loads(await ev(cdp, '__U16T.cellPt(%d,%d)' % (q['c'], q['r'])))
        pts.append((v[0], v[1]))
    if mode == 'mouse':
        await cdp.call('Input.dispatchMouseEvent', type='mousePressed', x=pts[0][0], y=pts[0][1],
                       button='left', buttons=1, clickCount=1)
    else:
        await cdp.call('Input.dispatchTouchEvent', type='touchStart',
                       touchPoints=[{'x': pts[0][0], 'y': pts[0][1], 'id': 0}])
    maxd = 1
    for i in range(1, len(pts)):
        px, py = pts[i-1]
        for k in range(1, 9):
            x = px + (pts[i][0]-px)*k/8.0; y = py + (pts[i][1]-py)*k/8.0
            if mode == 'mouse':
                await cdp.call('Input.dispatchMouseEvent', type='mouseMoved', x=x, y=y,
                               button='left', buttons=1, clickCount=0)
            else:
                await cdp.call('Input.dispatchTouchEvent', type='touchMove',
                               touchPoints=[{'x': x, 'y': y, 'id': 0}])
        d = await ev(cdp, "drag ? drag.path.length : -1")
        maxd = max(maxd, d if isinstance(d, int) else -1)
    if mode == 'mouse':
        await cdp.call('Input.dispatchMouseEvent', type='mouseReleased', x=pts[-1][0],
                       y=pts[-1][1], button='left', buttons=0, clickCount=1)
    else:
        await cdp.call('Input.dispatchTouchEvent', type='touchEnd', touchPoints=[])
    return maxd

def st(v):
    try: return json.loads(v)
    except Exception: return None

async def play(cdp, mode, tag, fails):
    # deterministic run (harness glue: newGame is the page's own commit, seed fixed)
    await ev(cdp, "localStorage.clear()")
    if not await wait_for(cdp, "typeof api !== 'undefined' && api !== null", timeout=40):
        fails.append(tag + ': api timeout'); return
    await ev(cdp, HELPERS)
    if not await wait_for(cdp, "art && art.tex && art.tex.menus && art.tex.menus.width > 0", timeout=40):
        fails.append(tag + ': art not loaded'); return
    await ev(cdp, "newGame(0x%X, 1, 3, 0)" % SEED)
    if not await wait_for(cdp, "curScreen === 'scrGame'", timeout=10):
        fails.append(tag + ': newGame did not reach the board'); return
    if not await wait_for(cdp, "!busy()", timeout=15):
        fails.append(tag + ': board never settled'); return
    # drag 1: plain chain — must form and advance the turn
    path = json.loads(await ev(cdp, "__U16T.findChain()"))
    if not path or path == 'null':
        fails.append(tag + ': no legal 3+ chain on the seed board'); return
    before = st(await ev(cdp, "__U16T.state()"))
    d1 = await drag(cdp, mode, path)
    await wait_for(cdp, "!busy()", timeout=8)
    after = st(await ev(cdp, "__U16T.state()"))
    ok1 = d1 >= 3 and before and after and after['turn'] > before['turn'] \
          and before['hash'] != after['hash']
    print('%s drag1 (plain): maxDrag=%d turn %s->%s %s'
          % (tag, d1, before['turn'], after['turn'], 'OK' if ok1 else 'FAIL'))
    if not ok1:
        fails.append('%s: plain drag did not form/advance (maxDrag=%d, turn %s->%s)'
                     % (tag, d1, before['turn'], after['turn']))
    # drag 2: chain STARTING UNDER THE LIVE TIP (the U16 eaten-tap regression guard)
    if st(await ev(cdp, "__U16T.state()"))['tip'] < 0:
        fails.append(tag + ': no help bubble after the first chain — guard is vacuous'); return
    pathU = json.loads(await ev(cdp, "__U16T.chainUnderTip()"))
    if not pathU or pathU == 'null':
        fails.append(tag + ': no 3+ chain starts under the tip box on this board'); return
    before2 = st(await ev(cdp, "__U16T.state()"))
    d2 = await drag(cdp, mode, pathU)
    await wait_for(cdp, "!busy()", timeout=8)
    after2 = st(await ev(cdp, "__U16T.state()"))
    ok2 = d2 >= 3 and before2 and after2 and after2['turn'] > before2['turn'] \
          and before2['hash'] != after2['hash']
    print('%s drag2 (under tip N%d, start (%d,%d)): maxDrag=%d turn %s->%s %s'
          % (tag, before2['tip'], pathU[0]['c'], pathU[0]['r'], d2,
             before2['turn'], after2['turn'], 'OK' if ok2 else 'FAIL'))
    if not ok2:
        fails.append('%s: drag under the help bubble was EATEN (maxDrag=%d, tip=%s) — '
                     'the U16 swipe-break regressed'
                     % (tag, d2, before2['tip']))
    shot = await cdp.call('Page.captureScreenshot', format='png')
    open(os.path.join(SHOTS, 'test_u16_' + tag + '.png'), 'wb').write(base64.b64decode(shot['data']))

async def run(port):
    prof = tempfile.mkdtemp(prefix='u16t_prof_')
    proc = subprocess.Popen(
        [CHROME, '--headless=new', '--hide-scrollbars', '--force-device-scale-factor=1',
         '--no-first-run', '--no-default-browser-check', '--mute-audio', '--no-sandbox',
         '--disable-gpu', '--disable-dev-shm-usage', '--user-data-dir=' + prof,
         '--window-size=1280,800', '--remote-debugging-port=0', 'about:blank'],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fails = []
    try:
        pf = os.path.join(prof, 'DevToolsActivePort')
        for _ in range(600):
            if os.path.exists(pf): break
            time.sleep(0.1)
        dport = int(open(pf).read().split('\n')[0])
        ws_url = None
        for _ in range(240):
            try:
                t = json.loads(urllib.request.urlopen(
                    'http://127.0.0.1:%d/json/list' % dport, timeout=2).read().decode())
                pg = next((x for x in t if x.get('type') == 'page'), None)
                if pg: ws_url = pg['webSocketDebuggerUrl']; break
            except Exception:
                await asyncio.sleep(0.25)
        if not ws_url: return ['no CDP endpoint']
        import websockets
        async with websockets.connect(ws_url, max_size=64 << 20) as ws:
            cdp = CDP(ws)
            await cdp.call('Page.enable'); await cdp.call('Runtime.enable')
            url = 'http://127.0.0.1:%d/dungeon-raid/demo-art/index.html' % port
            for mode, (w, h, dpr, mob) in (('mouse', (1280, 800, 1, False)),
                                           ('touch', (390, 844, 3, True))):
                await cdp.call('Emulation.setDeviceMetricsOverride', width=w, height=h,
                               deviceScaleFactor=dpr, mobile=mob)
                await cdp.call('Emulation.setTouchEmulationEnabled',
                               enabled=(mode == 'touch'), maxTouchPoints=1)
                await cdp.call('Page.navigate', url=url)
                if not await wait_for(cdp, "document.readyState === 'complete'"):
                    fails.append('%s: load timeout' % mode); continue
                await play(cdp, mode, mode, fails)
            fails.extend('console/exception: ' + e for e in cdp.events)
    finally:
        proc.kill()
        try: proc.wait(timeout=30)
        except Exception: pass
    return fails

def build_tree():
    tree = tempfile.mkdtemp(prefix='u16t_tree_')
    os.makedirs(os.path.join(tree, 'dungeon-raid', 'demo-art'))
    html = open(os.path.join(HERE, 'demo_art.html'), encoding='utf-8').read()
    html = html.replace('../../assets/ripped', '../assets/ripped')
    open(os.path.join(tree, 'dungeon-raid', 'demo-art', 'index.html'), 'w',
         encoding='utf-8').write(html)
    os.makedirs(os.path.join(tree, 'dungeon-raid', 'assets'))
    os.symlink(os.path.join(ROOT, 'assets', 'ripped'),
               os.path.join(tree, 'dungeon-raid', 'assets', 'ripped'))
    return tree

if __name__ == '__main__':
    os.makedirs(SHOTS, exist_ok=True)
    print('mem available: %d MB' % mem_guard())
    tree = build_tree()
    port = free_port()
    httpd = http.server.ThreadingHTTPServer(('127.0.0.1', port),
                                            functools.partial(Quiet, directory=tree))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    fails = asyncio.run(run(port))
    httpd.shutdown()
    for f in fails:
        print('FAIL: ' + f)
    print('test_u16: ' + ('PASS' if not fails else 'FAIL (%d)' % len(fails)))
    sys.exit(1 if fails else 0)
