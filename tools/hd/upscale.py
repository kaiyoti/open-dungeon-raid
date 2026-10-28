#!/usr/bin/env python3
# Tooling harness (NOT game logic). X2 HD-upscale pilot.
# Upscales one ripped atlas 4x PER SPRITE (per PLAN M14): cut each sprites.json / .fnt
# glyph region, edge-extend-pad it, upscale RGB and alpha separately, trim the pad back
# off, and paste the result into a 4x atlas at 4x coordinates so neighbours never bleed.
# Methods: esrgan (Real-ESRGAN 4x anime, CPU PyTorch), xbrz (reference xBRZ 4x) and
# animesharp (4x-AnimeSharp via spandrel, the X4 user pick 2026-10-27; run with
# ~/hd-tools/venv/bin/python — spandrel lives in that venv).
# X4 (master note 2026-10-26): --base = whole-atlas upscale with the SAME method as the BASE
# layer, then the per-sprite results pasted on top, so page rects NOT in sprites.json
# (X1 SURVEY: tiles 31% / menus 42% opaque px outside sprites.json) are covered too.
# Region source and per-sprite split follow assets/ripped/sprites.json (the same rects the
# page uses). Cite: models/bin built in tasks/X2/progress.md (STEP 1);
# animesharp loader = tasks/X2b/scripts/run_variants.py.

import argparse
import json
import os
import subprocess
import sys
import time

import numpy as np
from PIL import Image

HD = os.path.expanduser('~/hd-tools')
XBRZ_BIN = os.path.join(HD, 'bin', 'xbrz_raw')
# O1: portable default (script-relative; repo assets/ripped) — no machine paths in the tree.
RIPPED = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../assets/ripped')
SCALE = 4
PAD = 4

# atlas file stem -> sprites.json "tex" key (splash has no sprite regions: whole image)
ATLAS_TEX = {
    'tiles': 'tiles', 'icons': 'icons', 'menus': 'menus',
    'font': 'font', 'font_menu': 'fontMenu', 'font_score': 'fontScore',
    'splash': None,
}
# font atlas stem -> (sprites.json entry key, .fnt file)
FONT_ENTRY = {'font': 'font:font', 'font_menu': 'font:fontMenu', 'font_score': 'font:fontScore'}
FONT_FNT = {'font': 'font.fnt', 'font_menu': 'font_menu.fnt', 'font_score': 'font_score.fnt'}


def load_atlas_rgba(path):
    """Read any ripped atlas (RGBA / RGB / P indexed / LA gray+alpha) as RGBA."""
    return np.array(Image.open(path).convert('RGBA'))


def parse_fnt_glyphs(fnt_path):
    """AngelCode .fnt -> {char: (x, y, w, h)} (tooling: mirrors sprites.json glyphs)."""
    glyphs = {}
    with open(fnt_path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith('char id='):
                continue
            kv = {}
            for tok in line.split()[1:]:
                if '=' in tok:
                    k, v = tok.split('=', 1)
                    kv[k] = v
            cid = int(kv['id'])
            glyphs[chr(cid)] = (int(kv['x']), int(kv['y']), int(kv['width']), int(kv['height']))
    return glyphs


def regions_for(atlas_stem, atlas_w, atlas_h, sprites, regions_mode):
    """Return list of dicts: {name, x, y, w, h, frames, full} for this atlas."""
    tex = ATLAS_TEX[atlas_stem]
    out = []
    if tex is None:  # splash: whole image, no per-sprite split, no pad/trim
        out.append({'name': 'splash', 'x': 0, 'y': 0, 'w': atlas_w, 'h': atlas_h, 'frames': 1, 'full': True})
        return out
    if tex in ('tiles', 'icons', 'menus'):
        for name, v in sprites.items():
            if v.get('tex') != tex:
                continue
            out.append({'name': name, 'x': v['x'], 'y': v['y'], 'w': v['w'], 'h': v['h'],
                        'frames': v.get('frames', 1), 'full': False})
        return out
    # font atlases: glyph regions
    entry = sprites[FONT_ENTRY[atlas_stem]]
    if regions_mode == 'fnt':
        glyphs = parse_fnt_glyphs(os.path.join(RIPPED, FONT_FNT[atlas_stem]))
    else:
        glyphs = entry['glyphs']
    for ch, (x, y, w, h) in glyphs.items():
        out.append({'name': 'glyph %s' % repr(ch)[1:-1], 'ch': ch, 'x': x, 'y': y, 'w': w, 'h': h,
                    'frames': 1, 'full': False})
    return out


def crop_padded_sprite(arr, x, y, w, h, pad):
    """Cut the sprite's OWN rect, then edge-extend-pad it (replicates the sprite's own
    border, never a neighbour's pixels). Always returns (h+2pad, w+2pad, 4)."""
    sprite = arr[y:y + h, x:x + w]
    if pad == 0:
        return sprite.copy()
    return np.pad(sprite, ((pad, pad), (pad, pad), (0, 0)), mode='edge')


def xbrz_rgba(rgba, scale=SCALE):
    h, w = rgba.shape[:2]
    tmp_in = os.path.join(HD, 'test', '_up_in.raw')
    tmp_out = os.path.join(HD, 'test', '_up_out.raw')
    with open(tmp_in, 'wb') as f:
        f.write(np.ascontiguousarray(rgba).tobytes())
    subprocess.run([XBRZ_BIN, str(scale), tmp_in, str(w), str(h), tmp_out], check=True)
    return np.frombuffer(open(tmp_out, 'rb').read(), dtype=np.uint8).reshape(h * scale, w * scale, 4).copy()


def xbrz_alpha(alpha, scale=SCALE):
    """Upscale a single-channel alpha with xBRZ (feed it as gray RGBA, take channel 0)."""
    rgb = np.dstack([alpha] * 3 + [np.full(alpha.shape, 255, np.uint8)])
    return xbrz_rgba(rgb, scale)[:, :, 0]


class Esrgan:
    """Loads the Real-ESRGAN 4x anime net once and reuses it for every sprite."""
    def __init__(self, model_name='anime_6b', tile=0):
        from basicsr.archs.rrdbnet_arch import RRDBNet
        from realesrgan import RealESRGANer
        if model_name == 'anime_6b':
            model = RRDBNet(num_in_ch=3, num_out_ch=3, num_feat=64, num_block=6, num_grow_ch=32, scale=4)
            pretrained = os.path.join(HD, 'models', 'RealESRGAN_x4plus_anime_6B.pth')
        elif model_name == 'animevideov3':
            from realesrgan.archs.srvgg_arch import SRVGGNetCompact
            model = SRVGGNetCompact(num_in_ch=3, num_out_ch=3, num_feat=64, num_conv=16, upscale=4, act_type='prelu')
            pretrained = os.path.join(HD, 'models', 'realesr-animevideov3.pth')
        else:
            raise SystemExit('unknown esrgan model ' + model_name)
        # pre_pad=0 on purpose: RealESRGANer's default pads with zeros (black frame); we supply
        # our own edge-extended padding instead. tile=0 (sprites are small); tile=64 if RAM-capped.
        self.up = RealESRGANer(scale=4, model_path=pretrained, model=model, tile=tile, half=False, pre_pad=0)

    def rgb(self, rgb):
        out_bgr, _ = self.up.enhance(rgb[:, :, ::-1], outscale=4)  # RealESRGANer is cv2-BGR
        return out_bgr[:, :, ::-1].copy()


def net_up(net, rgb, tile=160, ctx=16, scale=SCALE):
    """4x net upscale with context tiling (tooling: from tasks/X2b run_variants.py;
    avoids OOM on whole atlases). Input RGB uint8 -> 4x RGB uint8."""
    import torch
    h, w = rgb.shape[:2]
    S = scale
    out = np.zeros((h * S, w * S, 3), np.uint8)
    x = torch.from_numpy(rgb.astype(np.float32) / 255.)
    with torch.no_grad():
        for ty in range(0, h, tile):
            for tx in range(0, w, tile):
                c0 = max(ty - ctx, 0); c1 = min(ty + tile + ctx, h)
                r0 = max(tx - ctx, 0); r1 = min(tx + tile + ctx, w)
                inp = x[c0:c1, r0:r1].permute(2, 0, 1).unsqueeze(0)
                y = net(inp)[0].permute(1, 2, 0).numpy()
                y = (y * 255).clip(0, 255).astype(np.uint8)
                oy = (ty - c0) * S; ox = (tx - r0) * S
                th = (min(ty + tile, h) - ty) * S; tw = (min(tx + tile, w) - tx) * S
                out[ty * S:ty * S + th, tx * S:tx * S + tw] = y[oy:oy + th, ox:ox + tw]
    return out


class AnimeSharp:
    """4x-AnimeSharp (user pick 2026-10-27, tasks/X2b): spandrel loader, RGB only;
    alpha stays xBRZ (X2 convention). Needs ~/hd-tools/venv (spandrel 0.4.2)."""
    def __init__(self, tile=160, ctx=16):
        import torch
        torch.set_num_threads(12)   # X2b setting
        from spandrel import ModelLoader
        m = ModelLoader().load_from_file(os.path.join(HD, 'models', '4x-AnimeSharp.pth')).model
        m.eval()
        self.net, self.tile, self.ctx = m, tile, ctx

    def rgb(self, rgb):
        return net_up(self.net, rgb, self.tile, self.ctx)


def fit(arr, h, w):
    if arr.shape[0] == h and arr.shape[1] == w:
        return arr
    return np.array(Image.fromarray(arr).resize((w, h), Image.NEAREST))


def upscale_atlas(atlas_path, out_path, method, regions_mode='sprites.json',
                  model_name='anime_6b', tile=0, scale=SCALE, pad=PAD, verbose=True,
                  base=False):
    stem = os.path.splitext(os.path.basename(atlas_path))[0]
    if stem not in ATLAS_TEX:
        raise SystemExit('unknown atlas ' + stem)
    sprites = json.load(open(os.path.join(RIPPED, 'sprites.json')))
    arr = load_atlas_rgba(atlas_path)
    ah, aw = arr.shape[:2]
    regions = regions_for(stem, aw, ah, sprites, regions_mode)

    upl = None
    if method == 'esrgan':
        upl = Esrgan(model_name, tile)
    elif method == 'animesharp':
        upl = AnimeSharp()

    t_total = 0.0
    if base:
        # X4 base layer: whole atlas through the SAME method (RGB) + xBRZ alpha, so every
        # page rect (incl. the ~120 hardcoded tiles.png rects NOT in sprites.json, X1) has
        # HD pixels; per-sprite results are pasted on top below.
        t0 = time.time()
        if method == 'xbrz':
            rgb_up = xbrz_rgba(arr, scale)[:, :, :3]
        else:
            rgb_up = upl.rgb(arr[:, :, :3])
        alpha_up = xbrz_alpha(arr[:, :, 3], scale)
        out = np.dstack([fit(rgb_up, ah * scale, aw * scale),
                         fit(alpha_up, ah * scale, aw * scale)])
        t_total += time.time() - t0
        if verbose:
            print('  base layer %s %dx%d  %.2fs' % (method, aw * scale, ah * scale, t_total))
    else:
        out = np.zeros((ah * scale, aw * scale, 4), np.uint8)  # transparent background

    n_frames = 0
    for r in regions:
        if base and r['full']:
            continue  # splash: the base layer IS the whole-image upscale
        p = 0 if r['full'] else pad
        frames = r['frames']
        fw = r['w'] // frames
        for i in range(frames):
            fx = r['x'] + i * fw
            crop = crop_padded_sprite(arr, fx, r['y'], fw, r['h'], p)
            t0 = time.time()
            if method == 'xbrz':
                up = xbrz_rgba(crop, scale)
            else:
                rgb_up = upl.rgb(crop[:, :, :3])
                alpha_up = xbrz_alpha(crop[:, :, 3], scale)
                up = np.dstack([fit(rgb_up, rgb_up.shape[0], rgb_up.shape[1]), alpha_up])
            dt = time.time() - t0
            t_total += dt
            n_frames += 1
            t = p * scale
            up = up[t:up.shape[0] - t, t:up.shape[1] - t]
            up = fit(up, r['h'] * scale, fw * scale)
            oy, ox = r['y'] * scale, fx * scale
            out[oy:oy + r['h'] * scale, ox:ox + fw * scale] = up
    Image.fromarray(out).save(out_path)
    if verbose:
        print('%-6s %-11s regions=%d frames=%d out=%dx%d  upscale=%.2fs' %
              (method, stem, len(regions), n_frames, out.shape[1], out.shape[0], t_total))
    return t_total, len(regions), n_frames


def main():
    ap = argparse.ArgumentParser(description='X2/X4 per-sprite HD upscale (tooling harness)')
    ap.add_argument('method', choices=['esrgan', 'xbrz', 'animesharp'])
    ap.add_argument('in_png')
    ap.add_argument('out_png')
    ap.add_argument('--regions', default='sprites.json', choices=['sprites.json', 'fnt'])
    ap.add_argument('--model', default='anime_6b', choices=['anime_6b', 'animevideov3'])
    ap.add_argument('--tile', type=int, default=0)
    ap.add_argument('--scale', type=int, default=SCALE)
    ap.add_argument('--pad', type=int, default=PAD)
    ap.add_argument('--base', action='store_true',
                    help='whole-atlas upscale (same method) as base layer, per-sprite on top (X4)')
    args = ap.parse_args()
    os.makedirs(os.path.dirname(os.path.abspath(args.out_png)), exist_ok=True)
    upscale_atlas(args.in_png, args.out_png, args.method, args.regions,
                  args.model, args.tile, args.scale, args.pad, base=args.base)


if __name__ == '__main__':
    main()
