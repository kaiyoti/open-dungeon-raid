#!/usr/bin/env bash
# O1 / AMENDMENT 2 — OPTIONAL "bring your own APK" refresh of assets/ripped.
#
# The public repo SHIPS assets/ripped (preservation release), so this script is not needed
# to build or play. It exists for users who want to re-extract the game's art/sounds/fonts
# from a Dungeon Raid APK THEY own (or to restore files they deleted — sprites.json ships
# with the repo and is NOT part of the APK: `git checkout -- assets/ripped` restores it).
#
#   tools/setup_assets.sh /path/to/DungeonRaid.apk [options]
#     --force         overwrite existing files in assets/ripped (default: skip extraction)
#     --moolbor PATH  MOOLBOR.TTF for the optional HD font regen (else the search order
#                     inside tools/hd/regen_fonts.py; never fetched automatically)
#     --no-fonts      do not run the HD font-atlas regeneration
#     --fonts-only    only run the font regeneration (no APK argument needed)
#
# The optional HD font step needs: python3 with numpy + pillow (pip install numpy pillow).
# Without MOOLBOR.TTF it falls back to the bundled Della Respira (SIL OFL 1.1, fonts/).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TPL="$ROOT/rebuild/web/template.html"
RIPOUT="$ROOT/assets/ripped"

FORCE=0; DO_FONTS=1; FONTS_ONLY=0; MOOLBOR=""; APK=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --force) FORCE=1 ;;
    --no-fonts) DO_FONTS=0 ;;
    --fonts-only) FONTS_ONLY=1 ;;
    --moolbor) MOOLBOR="$2"; shift ;;
    -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) if [ -z "$APK" ]; then APK="$1"; else echo "unexpected argument: $1" >&2; exit 2; fi ;;
  esac
  shift
done

# ---------------------------------------------------------------- extract from the APK
extract=1
if [ -f "$RIPOUT/sprites.json" ] && [ "$FORCE" = 0 ]; then
  echo "assets/ripped already present (it ships with this repo) — skipping extraction"
  echo "(use --force to re-extract from your APK anyway)"
  extract=0
fi
if [ "$FONTS_ONLY" = 1 ]; then extract=0; fi

if [ "$extract" = 1 ]; then
  [ -n "$APK" ] || { echo "usage: tools/setup_assets.sh /path/to/DungeonRaid.apk [--force] [--moolbor P] [--no-fonts]" >&2; exit 2; }
  [ -f "$APK" ] || { echo "FATAL: APK not found: $APK" >&2; exit 1; }
  unzip -l "$APK" >/dev/null 2>&1 || { echo "FATAL: not a zip/APK: $APK" >&2; exit 1; }

  TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
  mkdir -p "$RIPOUT/sounds"

  echo "extracting art/ogg/fnt/ttf from $(basename "$APK") ..."
  # atlas pages + splash (res/drawable-nodpi-v4)
  unzip -o -j -q "$APK" \
    'res/drawable-nodpi-v4/font.png' 'res/drawable-nodpi-v4/font_menu.png' \
    'res/drawable-nodpi-v4/font_score.png' 'res/drawable-nodpi-v4/icons.png' \
    'res/drawable-nodpi-v4/menus.png' 'res/drawable-nodpi-v4/tiles.png' \
    'res/drawable-nodpi-v4/splash.png' -d "$TMP" || { echo "FATAL: atlas pages missing from APK" >&2; exit 1; }
  # bitmap-font descriptors (page embeds the data, but the optional HD regen reads them)
  unzip -o -j -q "$APK" 'res/raw/font.fnt' 'res/raw/font_menu.fnt' 'res/raw/font_score.fnt' -d "$TMP" \
    || { echo "FATAL: .fnt descriptors missing from APK" >&2; exit 1; }
  # heading font (see README: Dwarven Stonecraft, 100% Free)
  unzip -o -j -q "$APK" 'assets/fonts/dwarvesfont.ttf' -d "$TMP" \
    || { echo "FATAL: assets/fonts/dwarvesfont.ttf missing from APK" >&2; exit 1; }
  # every sound effect (the page picks from SOUND_FILES x SOUND_VARIATIONS)
  unzip -o -j -q "$APK" 'res/raw/*.ogg' -d "$TMP" \
    || { echo "FATAL: res/raw/*.ogg missing from APK" >&2; exit 1; }

  cp "$TMP/font.png" "$TMP/font_menu.png" "$TMP/font_score.png" "$TMP/icons.png" \
     "$TMP/menus.png" "$TMP/tiles.png" "$TMP/splash.png" "$RIPOUT/"
  cp "$TMP/font.fnt" "$TMP/font_menu.fnt" "$TMP/font_score.fnt" "$RIPOUT/"
  cp "$TMP/dwarvesfont.ttf" "$RIPOUT/"
  cp "$TMP"/*.ogg "$RIPOUT/sounds/"

  # verify: every file the page fetches (ASSET_BASE literals + the sound tables) exists
  python3 - "$TPL" "$RIPOUT" <<'PY'
import re, sys, os, os.path
tpl, out = sys.argv[1], sys.argv[2]
src = open(tpl, encoding='utf-8').read()
paths = set()
for m in re.finditer(r"(?:ASSET_BASE|audioBase)\s*\+\s*'(/[^']+)'", src):
    p = m.group(1).rstrip(')')
    if p.startswith('/sounds/'):
        continue
    paths.add(p.lstrip('/'))
def arr(name, kind):
    m = re.search(r"const\s+" + name + r"\s*=\s*(\[[^\]]*\])\s*;", src, re.S)
    body = m.group(1)
    return re.findall(r"'([^']*)'", body) if kind == 'str' else [int(x) for x in re.findall(r"\d+", body)]
for base, n in zip(arr('SOUND_FILES', 'str'), arr('SOUND_VARIATIONS', 'num')):
    for v in range(1, int(n) + 1):
        paths.add('sounds/%s%d.ogg' % (base, v))
missing = [p for p in sorted(paths) if not os.path.isfile(os.path.join(out, p))]
if missing:
    print('FATAL: page fetches %d missing file(s):' % len(missing), file=sys.stderr)
    for p in missing[:20]:
        print('  ', p, file=sys.stderr)
    sys.exit(1)
print('verified: all %d page-fetched files present in %s' % (len(paths), out))
PY
fi

# ---------------------------------------------------------------- optional HD font regen
if [ "$DO_FONTS" = 1 ]; then
  if ! python3 -c 'import numpy, PIL' 2>/dev/null; then
    echo "FATAL: HD font regeneration needs numpy + pillow:  pip install numpy pillow" >&2
    echo "       (or re-run with --no-fonts)" >&2
    exit 1
  fi
  echo "regenerating HD font atlases (tools/hd/regen_fonts.py --write) ..."
  if [ -n "$MOOLBOR" ]; then
    python3 "$ROOT/tools/hd/regen_fonts.py" --moolbor "$MOOLBOR" --write
  else
    python3 "$ROOT/tools/hd/regen_fonts.py" --write
  fi
fi
echo "done."
