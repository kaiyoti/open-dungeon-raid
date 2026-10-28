#!/usr/bin/env bash
# Build the WebAssembly rebuild demo (W1).
# U12 (user 2026-10-27, Original-only): the plain (shapes) build is DROPPED — every build is
# the Original theme.
#   web/build.sh          -> web/dr.js + web/demo_art.html  (original ripped art, LOCAL ONLY)
#   web/build.sh --art    -> web/dr.js + web/demo_art.html  (same)
#   web/build.sh --all    -> demo_art.html + demo_art_hd.html (X3/M14)
# X3/M14: demo_art_hd.html = the HD side variant — same page, ASSET_SCALE=4, assets from
# assets/ripped-hd (each atlas upscaled 4x, same layout; tools/hd + X4). LOCAL ONLY.
# Toolchain: Emscripten at ~/tools/emsdk (3.1.74).
set -euo pipefail
cd "$(dirname "$0")"          # rebuild/web
ROOT="$(cd ../.. && pwd)"     # dungeon-raid root (for assets/ripped in the art build)

TARGET=demo_art
if [[ ${1:-} == --art ]]; then TARGET=demo_art
elif [[ ${1:-} == --hd ]]; then TARGET=demo_art_hd
elif [[ ${1:-} == --all ]]; then TARGET=all
elif [[ -n ${1:-} ]]; then echo "usage: $0 [--art|--hd|--all]" >&2; exit 2; fi

# Optional machine env (this host); public builders need emsdk on PATH or EMSDK set.
# shellcheck disable=SC1091
[ -f "$HOME/tools/emsdk/emsdk_env.sh" ] && source "$HOME/tools/emsdk/emsdk_env.sh" >/dev/null 2>&1
command -v em++ >/dev/null 2>&1 || {
  echo "FATAL: em++ not found — install emsdk 3.1.74 and activate it (see README, 'Build the web game')" >&2
  exit 1; }

echo "=== em++ (this also picks up any rebuild/src changes, e.g. V5's menus.cpp) ==="
em++ -O2 -std=c++17 -I../include ../src/*.cpp api.cpp \
  -sSINGLE_FILE=1 -sMODULARIZE=1 -sEXPORT_NAME=DR \
  -sALLOW_MEMORY_GROWTH=1 -sEXPORTED_RUNTIME_METHODS=cwrap,HEAPU8 \
  -o dr.js
ls -l dr.js

inline() {  # $1 = template, $2 = out html, $3 = "true"/"false" (ART), $4 = ASSET_SCALE (1|4), $5 = page title (U14)
  python3 - "$1" "$2" "$3" "$4" "$5" <<'PY'
import base64, json, sys
tpl, out, art, scale, title = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5]
ASSET_BASE = '../../assets/ripped'   # A11b: ripped-asset base for BOTH builds (page-relative)
if scale != '1':
    ASSET_BASE = '../../assets/ripped-hd'   # X3: HD atlases live in their own dir
js = open('dr.js', encoding='utf-8').read()
html = open(tpl, encoding='utf-8').read()
assert '/*__DR_JS__*/' in html, 'template missing /*__DR_JS__*/ marker'
assert '__ART__' in html, 'template missing __ART__ marker'
assert '__ASSET_BASE__' in html, 'template missing __ASSET_BASE__ marker'
assert '__ASSET_SCALE__' in html, 'template missing __ASSET_SCALE__ marker'
assert '__TITLE__' in html, 'template missing __TITLE__ marker'
# U12: the __RECREATED__ embed is GONE with the Recreated theme (the template dropped the
# marker; the page is still ONE self-contained html file — dr.js inlined, art fetched).
html = html.replace('/*__DR_JS__*/', js).replace('__ART__', art)
html = html.replace('__ASSET_BASE__', ASSET_BASE).replace('__ASSET_SCALE__', scale)
html = html.replace('__TITLE__', title)
open(out, 'w', encoding='utf-8').write(html)
print(f'{out}: {len(html.encode()):,} bytes  (ASSET_SCALE={scale}, ASSET_BASE={ASSET_BASE})')
PY
}

if [[ $TARGET == demo_art || $TARGET == all ]]; then
  # the art build fetches ../assets/ripped/... relative to rebuild/web/ -> ROOT/assets/ripped
  inline template.html demo_art.html true 1 "Open Dungeon Raid"
fi
if [[ $TARGET == demo_art_hd || $TARGET == all ]]; then
  # X3/M14: HD variant fetches ../assets/ripped-hd/... (4x atlases, same layout)
  inline template.html demo_art_hd.html true 4 "Open Dungeon Raid HD"
fi
