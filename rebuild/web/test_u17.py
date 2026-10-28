#!/usr/bin/env python3
# U17 REGRESSION TEST (TEST HARNESS code only — not game logic):
# "still broken on desktop, using Edge browser, i think the cursor position is just wrong"
# (user 2026-10-28). Root cause: cellFromEvent() divided a cssSize-based
# boardDy by a rect.width-based cellPx, so when the canvas is CSS-scaled (letterbox makes the
# .screen wider than the cssSize cap of 480; browser/Windows zoom) the picked ROW shifted down
# by boardDy*(scale-1) px (row+1 for scale>1.5). Mobile fits the board to the screen width
# (scale 1) so it was unaffected. U16's test was blind: its cellPt() is the INVERSE of
# cellFromEvent, so it clicked where the mapping THOUGHT the cell was, not where the tile is
# DRAWN. This test clicks the GROUND-TRUTH visible centre (cellCenter scaled by the canvas's own
# rect) and asserts the picked cell == target for the dpr/zoom/size matrix, and that a real
# mouse drag of a legal 3+ chain (through the visible centres) forms and advances the turn on
# every config where the whole board fits on-screen. ONE headless Chrome host-wide (free -m
# >= 4000 gate). Fails pre-fix (row+1 on scale>1.5), passes post-fix.
import asyncio, functools, http.server, json, os, socket, subprocess, sys, tempfile, threading, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = HERE
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
CHROME = os.environ.get('CHROME', '/usr/bin/google-chrome')
if not os.path.exists(CHROME):
    CHROME = '/usr/bin/chromium'
SEED = 0x5517

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
window.__U17T = {
  geom: function(){
    var rect = canvas.getBoundingClientRect();
    return JSON.stringify({rw:rect.width, rh:rect.height, rl:rect.left, rt:rect.top,
      cssSize:cssSize, cell:cell, boardDy:boardDy, sx:rect.width/cssSize,
      sy:rect.height/(cssSize+boardDy), dpr:window.devicePixelRatio,
      innerW:window.innerWidth, innerH:window.innerHeight});
  },
  // GROUND TRUTH: on-screen CSS px centre of tile (c,r) = where draw() paints it
  // (cellCenter design px scaled by the canvas's own rendered rect).
  drawCenter: function(c,r){
    var rect = canvas.getBoundingClientRect();
    var sx = rect.width/cssSize, sy = rect.height/(cssSize+boardDy);
    var cc = cellCenter(c,r);
    return JSON.stringify([rect.left + cc.x*sx, rect.top + cc.y*sy]);
  },
  pick: function(x,y){ var h = cellFromEvent({clientX:x, clientY:y}); return h ? (h.c+','+h.r) : 'null'; },
  walk: function(sc,sr){
    function ok(cells){ var b=new Uint8Array(cells.map(function(q){return q.c+q.r*6;}));
      var p=api.buf(b.length); M.HEAPU8.set(b,p); return api.pathOk(p,b.length)===1; }
    var path=[{c:sc,r:sr}];
    if(!ok([path[0]])) return 'null';
    for(var s=0;s<5;s++){ var head=path[path.length-1], added=false;
      for(var dr=-1;dr<=1&&!added;dr++) for(var dc=-1;dc<=1&&!added;dc++){
        if(!dr&&!dc) continue;
        var nc=head.c+dc, nr=head.r+dr;
        if(nc<0||nc>5||nr<0||nr>5) continue;
        if(path.some(function(q){return q.c===nc&&q.r===nr;})) continue;
        if(ok(path.concat([{c:nc,r:nr}]))){path.push({c:nc,r:nr});added=true;}
      } }
    return path.length>=3 ? JSON.stringify(path) : 'null';
  },
  findChain: function(){
    for(var r=0;r<6;r++) for(var c=0;c<6;c++){ var p=window.__U17T.walk(c,r); if(p!=='null') return p; }
    return 'null';
  },
  state: function(){
    var h=0; for(var r=0;r<6;r++) for(var c=0;c<6;c++) h=(h*31+api.tile(c,r))>>>0;
    return JSON.stringify({turn:api.stat(8), over:api.over(), busy:busy(), hash:h});
  }
};
"""

async def boot(cdp, tag, fails):
    await ev(cdp, "localStorage.clear()")
    if not await wait_for(cdp, "typeof api !== 'undefined' && api !== null", timeout=40):
        fails.append(tag + ': api timeout'); return False
    await ev(cdp, HELPERS)
    if not await wait_for(cdp, "art && art.tex && art.tex.menus && art.tex.menus.width > 0", timeout=40):
        fails.append(tag + ': art not loaded'); return False
    await ev(cdp, "newGame(0x%X, 1, 3, 0)" % SEED)
    if not await wait_for(cdp, "curScreen === 'scrGame'", timeout=10):
        fails.append(tag + ': no board'); return False
    if not await wait_for(cdp, "!busy()", timeout=15):
        fails.append(tag + ': busy'); return False
    return True

async def mouse_drag(cdp, pts):
    await cdp.call('Input.dispatchMouseEvent', type='mousePressed', x=pts[0][0], y=pts[0][1],
                   button='left', buttons=1, clickCount=1)
    maxd = 1
    for i in range(1, len(pts)):
        px, py = pts[i-1]
        for k in range(1, 9):
            x = px + (pts[i][0]-px)*k/8.0; y = py + (pts[i][1]-py)*k/8.0
            await cdp.call('Input.dispatchMouseEvent', type='mouseMoved', x=x, y=y,
                           button='left', buttons=1, clickCount=0)
        d = await ev(cdp, "drag ? drag.path.length : -1")
        maxd = max(maxd, d if isinstance(d, int) else -1)
    await cdp.call('Input.dispatchMouseEvent', type='mouseReleased', x=pts[-1][0], y=pts[-1][1],
                   button='left', buttons=0, clickCount=1)
    return maxd

async def probe(cdp, tag, fits, fails):
    if not await boot(cdp, tag, fails):
        return
    g = json.loads(await ev(cdp, "__U17T.geom()"))
    # 1) mapping: the cell picked at the VISIBLE centre of each cell == that cell.
    mapbad = []
    for (c, r) in ((0, 0), (2, 2), (5, 5)):
        dc = json.loads(await ev(cdp, "__U17T.drawCenter(%d,%d)" % (c, r)))
        pk = await ev(cdp, "__U17T.pick(%f,%f)" % (dc[0], dc[1]))
        if pk != '%d,%d' % (c, r):
            mapbad.append('%s->%s' % ('%d,%d' % (c, r), pk))
    # 2) drag: a real mouse drag through the visible centres of a legal 3+ chain forms + advances.
    dragmsg = 'skip (board off-screen)'
    if fits:
        path = json.loads(await ev(cdp, "__U17T.findChain()"))
        if not path or path == 'null':
            fails.append(tag + ': no legal 3+ chain on the seed board'); return
        pts = []
        off = False
        for q in path:
            dc = json.loads(await ev(cdp, "__U17T.drawCenter(%d,%d)" % (q['c'], q['r'])))
            if not (0 <= dc[0] <= g['innerW'] and 0 <= dc[1] <= g['innerH']):
                off = True
            pts.append((dc[0], dc[1]))
        if off:
            dragmsg = 'skip (chain off-screen)'
        else:
            before = json.loads(await ev(cdp, "__U17T.state()"))
            d = await mouse_drag(cdp, pts)
            await wait_for(cdp, "!busy()", timeout=8)
            after = json.loads(await ev(cdp, "__U17T.state()"))
            ok = d >= 3 and after['turn'] > before['turn'] and before['hash'] != after['hash']
            dragmsg = 'drag len=%d turn %s->%s %s' % (d, before['turn'], after['turn'], 'OK' if ok else 'FAIL')
            if not ok:
                fails.append('%s: drag through visible centres did not form/advance (len=%d, turn %s->%s)'
                             % (tag, d, before['turn'], after['turn']))
    if mapbad:
        fails.append('%s: picked cell != cell under cursor at %s (scale sx=%.3f)'
                     % (tag, ' '.join(mapbad), g['sx']))
    print('%-24s sx=%.3f sy=%.3f dpr=%.2f  map=%s  %s'
          % (tag, g['sx'], g['sy'], g['dpr'], 'OK' if not mapbad else 'BROKEN ' + ' '.join(mapbad), dragmsg))
    sys.stdout.flush()

# (tag, deviceMetrics{w,h,dpr,mobile}, cssZoom, boardFitsOnScreen)
MATRIX = [
    ('base 1280x800 dpr1',    (1280, 800, 1,    False), '1',    True),
    ('dpr1.5 1920x1080',      (1920, 1080, 1.5,  False), '1',    True),
    ('dpr2 2560x1440',        (2560, 1440, 2,    False), '1',    True),
    ('tall 800x1400',         (800, 1400, 1,     False), '1',    True),
    ('csszoom1.1 dpr1.25',    (1920, 1080, 1.25, False), '1.1',  False),
    ('csszoom1.25 dpr1.25',   (1920, 1080, 1.25, False), '1.25', False),
    ('csszoom1.5 dpr1.5',     (1920, 1080, 1.5,  False), '1.5',  False),
]

async def run(port):
    prof = tempfile.mkdtemp(prefix='u17t_prof_')
    proc = subprocess.Popen(
        [CHROME, '--headless=new', '--hide-scrollbars',
         '--no-first-run', '--no-default-browser-check', '--mute-audio', '--no-sandbox',
         '--disable-gpu', '--disable-dev-shm-usage', '--user-data-dir=' + prof,
         '--window-size=2560,1440', '--remote-debugging-port=0', 'about:blank'],
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
            for tag, (w, h, dpr, mob), zoom, fits in MATRIX:
                await cdp.call('Emulation.setDeviceMetricsOverride', width=w, height=h,
                               deviceScaleFactor=dpr, mobile=mob)
                await cdp.call('Emulation.setTouchEmulationEnabled', enabled=False, maxTouchPoints=1)
                await cdp.call('Page.navigate', url=url)
                if not await wait_for(cdp, "document.readyState === 'complete'"):
                    fails.append('%s: load timeout' % tag); continue
                if zoom != '1':
                    await ev(cdp, "document.documentElement.style.zoom=%r" % zoom)
                    await asyncio.sleep(0.3)
                await probe(cdp, tag, fits, fails)
            fails.extend('console/exception: ' + e for e in cdp.events)
    finally:
        proc.kill()
        try: proc.wait(timeout=30)
        except Exception: pass
    return fails

def build_tree():
    tree = tempfile.mkdtemp(prefix='u17t_tree_')
    os.makedirs(os.path.join(tree, 'dungeon-raid', 'demo-art'))
    html = open(os.path.join(WEB, 'demo_art.html'), encoding='utf-8').read()
    html = html.replace('../../assets/ripped', '../assets/ripped')
    open(os.path.join(tree, 'dungeon-raid', 'demo-art', 'index.html'), 'w',
         encoding='utf-8').write(html)
    os.makedirs(os.path.join(tree, 'dungeon-raid', 'assets'))
    os.symlink(os.path.join(ROOT, 'assets', 'ripped'),
               os.path.join(tree, 'dungeon-raid', 'assets', 'ripped'))
    return tree

if __name__ == '__main__':
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
    print('test_u17: ' + ('PASS' if not fails else 'FAIL (%d)' % len(fails)))
    sys.exit(1 if fails else 0)
