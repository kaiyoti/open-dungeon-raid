#!/usr/bin/env python3
"""Draw the Open Dungeon Raid placeholder launcher icon (O1 — nothing from the original game).

A chunky torch-on-stone motif drawn from scratch with PIL so every density is generated
from the same geometry. This is what a PUBLIC build ships (the original game's icon is
never distributed — android/build_apk.sh only replaces it when the builder's own private
extracted/ copy is present).

Usage:  python3 tools/make_placeholder_icon.py [outdir]
        outdir defaults to android/app/src/main/res  (writes mipmap-{ldpi,mdpi,hdpi,xhdpi})
Sizes:  ldpi 36, mdpi 48, hdpi 72, xhdpi 96 (the densities build_apk.sh/manifest use).
"""
import os
import sys

from PIL import Image, ImageDraw

SIZES = {'ldpi': 36, 'mdpi': 48, 'hdpi': 72, 'xhdpi': 96}

BG = (31, 36, 48)          # deep slate
BG_EDGE = (14, 17, 24)
STONE = (74, 84, 104)      # arch blocks
STONE_D = (52, 60, 76)
FLAME_OUT = (214, 92, 40)  # torch flame, outer
FLAME_IN = (244, 190, 74)  # inner flame
HANDLE = (110, 78, 46)


def draw_master(px=96):
    """Torch in a stone arch on a rounded dark tile, authored at 96 px and scaled down."""
    S = px / 96.0
    im = Image.new('RGBA', (px, px), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)

    def box(x0, y0, x1, y1):
        return [x0 * S, y0 * S, x1 * S - 1, y1 * S - 1]

    # rounded background tile
    d.rounded_rectangle(box(0, 0, 96, 96), radius=16 * S, fill=BG, outline=BG_EDGE,
                        width=max(1, int(3 * S)))
    # stone arch: two pillars + a 3-block semicircle suggestion
    d.rectangle(box(16, 46, 28, 84), fill=STONE, outline=STONE_D, width=max(1, int(2 * S)))
    d.rectangle(box(68, 46, 80, 84), fill=STONE, outline=STONE_D, width=max(1, int(2 * S)))
    d.rectangle(box(28, 24, 68, 34), fill=STONE, outline=STONE_D, width=max(1, int(2 * S)))
    d.rectangle(box(16, 36, 28, 46), fill=STONE, outline=STONE_D, width=max(1, int(2 * S)))
    d.rectangle(box(68, 36, 80, 46), fill=STONE, outline=STONE_D, width=max(1, int(2 * S)))
    # torch handle (centre, inside the arch)
    d.rectangle(box(44, 56, 52, 80), fill=HANDLE)
    d.rectangle(box(40, 52, 56, 58), fill=HANDLE)
    # flame: outer teardrop then inner flame
    d.polygon([(48 * S, 38 * S), (60 * S, 56 * S), (48 * S, 62 * S), (36 * S, 56 * S)],
              fill=FLAME_OUT)
    d.polygon([(48 * S, 44 * S), (55 * S, 56 * S), (48 * S, 60 * S), (41 * S, 56 * S)],
              fill=FLAME_IN)
    return im


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '../android/app/src/main/res')
    outdir = os.path.abspath(outdir)
    for name, px in SIZES.items():
        im = draw_master(px)              # same geometry at every density (coords x S)
        dst = os.path.join(outdir, 'mipmap-' + name, 'ic_launcher.png')
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        im.save(dst)
        print('wrote', dst, f'({px}x{px})')


if __name__ == '__main__':
    main()
