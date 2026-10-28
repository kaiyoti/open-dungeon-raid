#!/usr/bin/env python3
# X8 tooling harness (NOT game logic): regenerate the 4x HD font atlases from the TTFs.
#
# For every char id in the 1x .fnt: render the glyph from the TTF at size*ASSET_SCALE
# (44*4=176, 42*4, 19*4) white-on-transparent and place it at the SAME x,y,w,h *4 cell
# as the 1x atlas, so the HD .fnt == 1x .fnt * 4 and NO metric code changes (the page's
# fontScaleTables already multiplies the 1x sprites.json rects by ASSET_SCALE).
#
# Ground truth = the 1x ripped atlas (assets/ripped/font*.png): the TTF glyph's ink bbox
# (alpha>=8) is mapped EXACTLY onto 4x the 1x glyph's ink bbox inside the cell, so the TTF
# glyph sits in its scaled cell the way the 1x glyph sits in its cell. Fit report per glyph:
# ink bbox delta + alpha-weighted centroid delta (HD px) vs the 1x glyph *4, and IoU of the
# alpha>=128 mask vs the 4x-nearest 1x glyph.
#
# Baked styles reproduced from the 1x atlases (measured 2026-10-27, tasks/X8):
#  - font.fnt      (MoolBoran 44, padding 0): RGB is constant 255 (white), alpha = AA.
#  - font_score.fnt(MoolBoran 42, padding 2): alpha = clean AA glyph; RGB carries a baked
#    gray shading (the page tints by GL modulate texel.rgb*tint, so the shading must be
#    kept): reproduced by bilinear 4x upscale of the 1x cell RGB sampled under the new glyph.
#  - font_menu.fnt (Dwarven Stonecraft 19, padding 4): white core OVER a black soft outline
#    ring (1x: core L=255, ring L=0 with soft alpha; edge texels L=255*core/A_out).
#    Reproduced: ring = gaussian_blur(dilate(core, r)) * k, composited black-under-white;
#    (r, sigma, k) grid-searched against the 1x alpha (mean |A_new - 4x A_1x| over the cell).
#
# Output: assets/ripped-hd/{font,font_score,font_menu}.png (512x512 RGBA). The existing
# AnimeSharp atlases are kept as *_ai.png (copied once if not already present).
# The 1x ripped/ files and analysis/ are NEVER touched.

import argparse
import json
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from PIL import ImageFont

# O1: portable root (env ODR_ROOT overrides) — no machine paths in the tree.
ROOT = os.environ.get('ODR_ROOT', os.path.abspath(os.path.join(os.path.dirname(__file__), '../..')))
S = 4                      # ASSET_SCALE
INK_T = 8                  # alpha threshold for "ink" (bbox)
CORE_T = 128               # alpha threshold for the solid core

# ttf for 'font'/'font_score' (the BODY/score face) is resolved in main(): MoolBoran
# (MOOLBOR.TTF) when the builder has it, else the bundled Della Respira fallback (O1/user
# 2026-10-27). 'font_menu' (headings) always uses the user's own dwarvesfont.ttf that their
# setup extracted from their own APK.
FONTS = {
    'font':       dict(fnt='assets/ripped/font.fnt',       ttf=None,
                       one='assets/ripped/font.png',       out='assets/ripped-hd/font.png',
                       style='plain'),
    'font_score': dict(fnt='assets/ripped/font_score.fnt', ttf=None,
                       one='assets/ripped/font_score.png', out='assets/ripped-hd/font_score.png',
                       style='score'),
    'font_menu':  dict(fnt='assets/ripped/font_menu.fnt',  ttf='assets/ripped/dwarvesfont.ttf',
                       one='assets/ripped/font_menu.png',  out='assets/ripped-hd/font_menu.png',
                       style='menu'),
}

MOOLBOR_HINT = ('put MOOLBOR.TTF in assets/fonts-private/ (or pass --moolbor / set ODR_MOOLBOR); '
                'Windows: C:\\Windows\\Fonts\\MOOLBOR.TTF — optional feature "Sinhala '
                'Supplemental Fonts" (Settings > Apps > Optional features)')


def resolve_body_ttf(explicit=None):
    """MoolBoran when available, else the bundled Della Respira (SIL OFL 1.1). (path, name)."""
    cands = []
    if explicit:
        cands.append(explicit)
    env = os.environ.get('ODR_MOOLBOR')
    if env:
        cands.append(env)
    cands.append(os.path.join(ROOT, 'assets/fonts-private/MOOLBOR.TTF'))
    cands.append('C:/Windows/Fonts/MOOLBOR.TTF')
    for c in cands:
        if c and os.path.isfile(c):
            return c, 'MoolBoran'
    della = os.path.join(ROOT, 'fonts/DellaRespira-Regular.ttf')
    if os.path.isfile(della):
        print('NOTE: MoolBoran (MOOLBOR.TTF) not found — using the bundled Della Respira '
              '(SIL OFL 1.1) fallback for the body/score atlases.\n      To use MoolBoran: '
              + MOOLBOR_HINT, file=sys.stderr)
        return della, 'Della Respira'
    print('FATAL: no body font found — need MOOLBOR.TTF (' + MOOLBOR_HINT +
          ') or fonts/DellaRespira-Regular.ttf', file=sys.stderr)
    sys.exit(1)

MENU_GRID = [dict(r=r, sig=s, k=k, g=g)
             for r in (3, 5, 7) for s in (2.0, 3.0, 4.0, 5.0, 6.0)
             for k in (0.5, 0.65, 0.8, 1.0) for g in (1.0, 1.5, 2.0)]


def core_sharpen(core, g):
    """Steepen the core alpha's 128-crossing (sharper glyph edge); bbox-safe (the ink
    mapping re-anchors the >=8 bbox). g=1 keeps the raw TTF AA."""
    if g == 1.0:
        return core
    c = core.astype(np.float64)
    return np.clip(128.0 + (c - 128.0) * g, 0, 255).astype(np.uint8)


def core_edge_grad(a):
    """Mean gradient magnitude on the core boundary (alpha in [100,155]) — edge sharpness."""
    gy, gx = np.gradient(a.astype(float))
    g = np.hypot(gx, gy)
    m = (a >= 100) & (a <= 155)
    return float(g[m].mean()) if m.any() else 0.0


def parse_fnt(path):
    hdr, chars = {}, {}
    for line in open(path):
        line = line.strip()
        if line.startswith('info'):
            m = re.search(r'padding=([\d,]+)', line)
            hdr['padding'] = list(map(int, m.group(1).split(',')))
            hdr['size'] = int(re.search(r'size=(\d+)', line).group(1))
        elif line.startswith('common'):
            for k in ('lineHeight', 'base', 'scaleW', 'scaleH'):
                hdr[k] = int(re.search(k + r'=(\d+)', line).group(1))
        elif line.startswith('char '):
            d = {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', line)}
            chars[d['id']] = d
    return hdr, chars


def ink_stats(alpha):
    """(bbox(x0,y0,x1,y1) inclusive, centroid(x,y)) of alpha>=INK_T; None if empty."""
    m = alpha >= INK_T
    if not m.any():
        return None
    ys, xs = np.nonzero(m)
    w = alpha[ys, xs].astype(np.float64)
    cx = float((xs * w).sum() / w.sum())
    cy = float((ys * w).sum() / w.sum())
    return (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()), cx, cy)


def render_glyph(font, ch):
    """Glyph coverage (2D alpha, white-on-transparent) on a generous canvas."""
    px = font.size
    m = 2 * px
    im = Image.new('L', (4 * px + m, 4 * px + m), 0)
    ImageDraw.Draw(im).text((m // 2, m // 2), ch, font=font, fill=255)
    return np.array(im)


def menu_composite(core_a, r, sig, k):
    """core alpha (0..255) -> (A_out, RGB) with a black soft outline ring under a white core."""
    core = Image.fromarray(core_a)
    ring = core.filter(ImageFilter.MaxFilter(2 * r + 1)).filter(ImageFilter.GaussianBlur(sig))
    ring = np.array(ring).astype(np.float64) * k
    c = core_a.astype(np.float64)
    a_out = np.clip(c + ring * (1.0 - c / 255.0), 0, 255)
    rgb = np.where(a_out > 0, 255.0 * c / np.maximum(a_out, 1e-6), 0.0)
    return a_out, rgb


def place_into_cell(rgba, cell, ink1):
    """Scale the rendered glyph so its ink bbox maps onto 4x ink1 inside the cell.
    rgba: (H,W,4) rendered glyph (alpha = coverage). Returns (H*4? no) -> RGBA cell patch."""
    x, y, w, h = cell
    cw, ch = w * S, h * S
    tx0, ty0, tx1, ty1 = ink1[0] * S, ink1[1] * S, ink1[2] * S, ink1[3] * S
    st = ink_stats(rgba[..., 3])
    sx0, sy0, sx1, sy1 = st[:4]
    src = rgba[sy0:sy1 + 1, sx0:sx1 + 1]
    tw, th = tx1 - tx0 + 1, ty1 - ty0 + 1
    scaled = np.array(Image.fromarray(src).resize((tw, th), Image.LANCZOS))
    patch = np.zeros((ch, cw, 4), np.uint8)
    # paste with alpha compositing (cells never overlap, plain overwrite is fine)
    patch[ty0:ty0 + th, tx0:tx0 + tw] = scaled
    return patch


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--report', default=os.path.join(ROOT, 'assets/ripped-hd/regen_fonts_report.json'))
    ap.add_argument('--write', action='store_true', help='write the atlases into assets/ripped-hd/')
    ap.add_argument('--moolbor', default=None, help='path to MOOLBOR.TTF (else the search order above)')
    args = ap.parse_args()

    body_ttf, body_which = resolve_body_ttf(args.moolbor)
    FONTS['font']['ttf'] = FONTS['font_score']['ttf'] = body_ttf
    print('body/score face:', body_which, '->', body_ttf)

    for f in FONTS.values():
        for p in (f['fnt'], f['ttf'], f['one']):
            if not os.path.exists(os.path.join(ROOT, p)):
                print(f'FATAL: missing {p}', file=sys.stderr)
                sys.exit(1)

    report = {'scale': S, 'fonts': {}}
    for name, f in FONTS.items():
        hdr, chars = parse_fnt(os.path.join(ROOT, f['fnt']))
        one = np.array(Image.open(os.path.join(ROOT, f['one'])).convert('RGBA'))
        canvas = np.zeros((hdr['scaleH'] * S, hdr['scaleW'] * S, 4), np.uint8)
        size = hdr['size'] * S
        font = ImageFont.truetype(os.path.join(ROOT, f['ttf']), size)

        # ---- menu: grid-search ring+core params against the 1x alpha -------------------
        # pick the sharpest core (edge grad >= 0.95 x the AnimeSharp atlas) whose alpha
        # fidelity to the 1x is best; the X6 gate needs the on-screen text at least as
        # sharp as the AnimeSharp HD it replaces, while the ring stays faithful to 1x.
        best = None
        if f['style'] == 'menu':
            ai_p = f['out'].replace('.png', '_ai.png')
            ai_full = os.path.join(ROOT, ai_p) if os.path.exists(os.path.join(ROOT, ai_p)) \
                else (os.path.join(ROOT, f['out']) if os.path.exists(os.path.join(ROOT, f['out']))
                      else None)
            if ai_full:
                ai = np.array(Image.open(ai_full).convert('RGBA'))
            else:
                # O1/public fresh setup: no previous HD atlas to compare sharpness against —
                # baseline = the 1x atlas NEAREST-upscaled xS (their own extracted atlas).
                ai = np.array(Image.fromarray(one).resize(
                    (one.shape[1] * S, one.shape[0] * S), Image.NEAREST))
            ai_edges = [core_edge_grad(ai[d['y'] * S:d['y'] * S + d['height'] * S,
                                        d['x'] * S:d['x'] * S + d['width'] * S, 3])
                        for d in chars.values() if d['id'] != 32]
            ai_edge = float(np.mean(ai_edges))
            results = []
            for p in MENU_GRID:
                tot, n, edges = 0.0, 0, []
                for cid, d in chars.items():
                    if cid == 32:
                        continue
                    a1 = one[d['y']:d['y'] + d['height'], d['x']:d['x'] + d['width'], 3]
                    st1 = ink_stats(a1)
                    if not st1:
                        continue
                    core = core_sharpen(render_glyph(font, chr(cid)), p['g'])
                    a_out, _ = menu_composite(core, p['r'], p['sig'], p['k'])
                    patch = place_into_cell(np.dstack([np.zeros_like(a_out)] * 3 + [a_out]).astype(np.uint8),
                                            (0, 0, d['width'], d['height']), st1)
                    ref = np.array(Image.fromarray(a1).resize(
                        (d['width'] * S, d['height'] * S), Image.NEAREST))
                    tot += np.abs(patch[..., 3].astype(np.int32) - ref.astype(np.int32)).sum()
                    n += a1.size
                    edges.append(core_edge_grad(patch[..., 3]))
                results.append(dict(p, score=tot / n, edge=float(np.mean(edges))))
            sharp = [r for r in results if r['edge'] >= 0.95 * ai_edge]
            best = (min(sharp, key=lambda r: r['score']) if sharp
                    else max(results, key=lambda r: r['edge']))
            best = {k: (round(v, 2) if isinstance(v, float) else v) for k, v in best.items()}
            print(f'[{name}] menu ring/core best: {best} | edge-ref {ai_edge:.1f} '
                  f'(grid of {len(MENU_GRID)}, {len(sharp)} sharp)')

        rows, worst = [], dict(bbox=0.0, cent=0.0, bbox_g=None, cent_g=None, iou=1.0, iou_g=None)
        for cid in sorted(chars):
            d = chars[cid]
            cell = (d['x'], d['y'], d['width'], d['height'])
            a1 = one[cell[1]:cell[1] + cell[3], cell[0]:cell[0] + cell[2], 3]
            st1 = ink_stats(a1)
            if cid == 32 or st1 is None:
                rows.append(dict(ch=chr(cid), id=cid, skip='no 1x ink'))
                continue
            core = render_glyph(font, chr(cid))
            stt = ink_stats(core)
            if stt is None:
                rows.append(dict(ch=chr(cid), id=cid, skip='TTF rendered empty'))
                continue
            if f['style'] == 'menu':
                core = core_sharpen(core, best['g'])
                a_out, rgb = menu_composite(core, best['r'], best['sig'], best['k'])
                rgba = np.dstack([rgb[..., None].repeat(3, 2).astype(np.uint8),
                                  a_out.astype(np.uint8)])
            else:
                rgba = np.dstack([np.full_like(core, 255), np.full_like(core, 255),
                                  np.full_like(core, 255), core])
            patch = place_into_cell(rgba, cell, st1)
            if f['style'] == 'score':
                # baked gray shading: sample the 1x cell RGB (bilinear 4x) under the new glyph
                ref_rgb = np.array(Image.fromarray(
                    one[cell[1]:cell[1] + cell[3], cell[0]:cell[0] + cell[2], :3])
                    .resize((cell[2] * S, cell[3] * S), Image.BILINEAR))
                m = patch[..., 3] > 0
                patch[..., :3][m] = ref_rgb[m]
            canvas[cell[1] * S:cell[1] * S + cell[3] * S,
                   cell[0] * S:cell[0] * S + cell[2] * S] = patch

            # ---- fit check: new glyph vs 1x glyph *4 (HD px) ---------------------------
            st2 = ink_stats(patch[..., 3])
            bd = max(abs(st2[0] - st1[0] * S), abs(st2[1] - st1[1] * S),
                     abs(st2[2] - st1[2] * S), abs(st2[3] - st1[3] * S))
            cd = float(np.hypot(st2[4] - st1[4] * S, st2[5] - st1[5] * S))
            ref = np.array(Image.fromarray(a1).resize((cell[2] * S, cell[3] * S), Image.NEAREST))
            m1, m2 = ref >= CORE_T, patch[..., 3] >= CORE_T
            iou = float((m1 & m2).sum() / max((m1 | m2).sum(), 1))
            r = dict(ch=chr(cid), id=cid, bbox_delta=round(bd, 2), centroid_delta=round(cd, 2),
                     centroid_delta_1xpx=round(cd / S, 2), iou=round(iou, 3))
            rows.append(r)
            if bd > worst['bbox']:
                worst.update(bbox=bd, bbox_g=chr(cid))
            if cd > worst['cent']:
                worst.update(cent=cd, cent_g=chr(cid))
            if iou < worst['iou']:
                worst.update(iou=iou, iou_g=chr(cid))

        ious = [r['iou'] for r in rows if 'iou' in r]
        report['fonts'][name] = dict(
            chars=len([r for r in rows if 'iou' in r]),
            menu_ring=best,
            worst_bbox_px=round(worst['bbox'], 2), worst_bbox_char=worst['bbox_g'],
            worst_centroid_px=round(worst['cent'], 2), worst_centroid_char=worst['cent_g'],
            mean_iou=round(float(np.mean(ious)), 3), min_iou=round(worst['iou'], 3),
            min_iou_char=worst['iou_g'], rows=rows)
        print(f"[{name}] {report['fonts'][name]['chars']} glyphs | worst bbox {worst['bbox']:.2f} px "
              f"('{worst['bbox_g']}') worst centroid {worst['cent']:.2f} HD px "
              f"= {worst['cent'] / S:.2f} 1x px ('{worst['cent_g']}') "
              f"mean IoU {np.mean(ious):.3f} min {worst['iou']:.3f} ('{worst['iou_g']}')")

        if args.write:
            out = os.path.join(ROOT, f['out'])
            ai = out.replace('.png', '_ai.png')
            if not os.path.exists(ai):
                os.rename(out, ai)
                print(f'kept AnimeSharp atlas -> {ai}')
            Image.fromarray(canvas, 'RGBA').save(out)
            print(f'wrote {out}')

    os.makedirs(os.path.dirname(args.report), exist_ok=True)
    json.dump(report, open(args.report, 'w'), indent=1)
    print('report:', args.report)


if __name__ == '__main__':
    main()
