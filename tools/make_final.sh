#!/usr/bin/env bash
# B1 — M11 final art build. Builds dist/dungeon-raid-<version>/ as ONE folder:
#   index.html        = rebuild/web/demo_art.html with ASSET_BASE 'assets/ripped'
#   assets/ripped/    = ONLY the files the page fetches (list derived from the page's
#                       fetch/img/Audio/FontFace uses of ASSET_BASE/audioBase + the
#                       SOUND_FILES x SOUND_VARIATIONS tables; build FAILS if a
#                       fetched file is missing from assets/ripped)
#   MANIFEST.txt      = version, build date, git-less source hashes, file list + sha256
#                       (the file-list section is `sha256sum -c` verifiable)
# Version = M11-<YYYYMMDD>-<n>, n = next free index for today under dist/.
# X3/M14: --hd builds the HD side variant instead (M14/PLAN.md):
#   tools/make_final.sh --hd  ->  dist/dungeon-raid-HD-<YYYYMMDD>-<n>/ from
#   rebuild/web/demo_art_hd.html (ASSET_SCALE=4) + assets/ripped-hd (4x atlases,
#   same layout; tools/hd). Same structure, assets land in assets/ripped-hd/.
# Local build output. dist/ is gitignored — never committed.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HD=0
for A in "$@"; do
  case "$A" in
    --hd) HD=1 ;;
    *) echo "usage: $0 [--hd]" >&2; exit 2 ;;
  esac
done
if [ "$HD" = 1 ]; then
  SRC="$ROOT/rebuild/web/demo_art_hd.html"
  ASSETS="$ROOT/assets/ripped-hd"
  ADIR="ripped-hd"
else
  SRC="$ROOT/rebuild/web/demo_art.html"
  ASSETS="$ROOT/assets/ripped"
  ADIR="ripped"
fi
DIST="$ROOT/dist"
[ -f "$SRC" ] || { echo "FATAL: source page not found: $SRC" >&2; exit 1; }
[ -d "$ASSETS" ] || { echo "FATAL: assets dir not found: $ASSETS" >&2; exit 1; }

# ---- version: M11-<YYYYMMDD>-<n> (HD: HD-<YYYYMMDD>-<n>) ---------------------------------
DATE="$(date +%Y%m%d)"
if [ "$HD" = 1 ]; then
  VPREFIX="HD-$DATE"
  FULLPREFIX="dungeon-raid-HD-$DATE"
else
  VPREFIX="M11-$DATE"
  FULLPREFIX="dungeon-raid-M11-$DATE"
fi
n=1
while [ -d "$DIST/$FULLPREFIX-$n" ]; do n=$((n+1)); done
VERSION="$VPREFIX-$n"
OUT="$DIST/dungeon-raid-$VERSION"
[ ! -e "$OUT" ] || { echo "FATAL: output exists: $OUT" >&2; exit 1; }
mkdir -p "$OUT/assets/$ADIR/sounds"

# ---- index.html: demo_art(_hd) with ASSET_BASE 'assets/ripped(-hd)' ----------------------
OLD="let ASSET_BASE = '../../assets/$ADIR';"
NEW="let ASSET_BASE = 'assets/$ADIR';"
[ "$(grep -cF "$OLD" "$SRC")" -eq 1 ] || { echo "FATAL: expected exactly one ASSET_BASE default line in $SRC" >&2; exit 1; }
sed "s|$(printf '%s' "$OLD" | sed 's/[&/\]/\\&/g')|$(printf '%s' "$NEW" | sed 's/[&/\]/\\&/g')|" "$SRC" > "$OUT/index.html"
grep -qF "$NEW" "$OUT/index.html" && ! grep -qF "$OLD" "$OUT/index.html" \
  || { echo "FATAL: ASSET_BASE rewrite failed in index.html" >&2; exit 1; }

# ---- asset list: derived from the page (index.html), not hardcoded ------------------------
# Prints one relative path (under assets/ripped/) per line. Explicit fetch/img/FontFace
# literals + every sounds/<base><variant>.ogg the SOUND_FILES x SOUND_VARIATIONS tables
# can play (the page plays a random variant 1..N, so every variant is a fetched file).
LIST="$(python3 - "$OUT/index.html" <<'PY'
import re, sys, json
src = open(sys.argv[1], encoding='utf-8').read()
paths = set()
for m in re.finditer(r"(?:ASSET_BASE|audioBase)\s*\+\s*'(/[^']+)'", src):
    p = m.group(1).rstrip(')')  # "url(' + audioBase + '/tiles.png)' -> strip the CSS url() paren
    if p.startswith('/sounds/'):
        continue  # sound dir/prefix: enumerated from the tables below
    paths.add(p.lstrip('/'))
def arr(name, kind):
    m = re.search(r"const\s+" + name + r"\s*=\s*(\[[^\]]*\])\s*;", src, re.S)
    if not m:
        sys.exit("FATAL: page table not found: " + name)
    body = m.group(1)
    if kind == 'str':
        return re.findall(r"'([^']*)'", body)
    return [int(x) for x in re.findall(r"\d+", body)]
bases = arr('SOUND_FILES', 'str')
vars_ = arr('SOUND_VARIATIONS', 'num')
if len(bases) != len(vars_):
    sys.exit("FATAL: SOUND_FILES/SOUND_VARIATIONS length mismatch")
for base, nv in zip(bases, vars_):
    for v in range(1, int(nv) + 1):
        paths.add('sounds/%s%d.ogg' % (base, v))
for p in sorted(paths):
    print(p)
PY
)"
[ -n "$LIST" ] || { echo "FATAL: derived asset list is empty" >&2; exit 1; }

MISSING=0
while IFS= read -r rel; do
  [ -n "$rel" ] || continue
  if [ ! -f "$ASSETS/$rel" ]; then
    echo "FATAL: page fetches assets/$ADIR/$rel but it is missing from $ASSETS" >&2
    MISSING=1
    continue
  fi
  mkdir -p "$OUT/assets/$ADIR/$(dirname "$rel")"
  cp "$ASSETS/$rel" "$OUT/assets/$ADIR/$rel"
done <<< "$LIST"
[ "$MISSING" -eq 0 ] || { echo "FATAL: fetched files missing — build aborted" >&2; exit 1; }

# ---- MANIFEST.txt -------------------------------------------------------------------------
BUILD_DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
MAN="$OUT/MANIFEST.txt"
{
  if [ "$HD" = 1 ]; then
    echo "# Dungeon Raid HD — M14 side variant (Open Dungeon Raid — local build)"
    echo "# HD side variant (M14/PLAN.md): ASSET_SCALE=4, assets/$ADIR"
  else
    echo "# Dungeon Raid — M11 final art build (Open Dungeon Raid — local build)"
  fi
  echo "version: $VERSION"
  echo "build-date: $BUILD_DATE"
  if [ "$HD" = 1 ]; then
    echo "builder: tools/make_final.sh (local build --hd)"
  else
    echo "builder: tools/make_final.sh (local build)"
  fi
  echo "source (no git repo — sha256 of the build inputs):"
  echo "  $VERSION  src  $(sha256sum "$SRC" | cut -d' ' -f1)  rebuild/web/$(basename "$SRC")"
  echo "  $VERSION  src  $(sha256sum "${BASH_SOURCE[0]}" | cut -d' ' -f1)  tools/make_final.sh"
  while IFS= read -r rel; do
    [ -n "$rel" ] || continue
    echo "  $VERSION  src  $(sha256sum "$ASSETS/$rel" | cut -d' ' -f1)  assets/$ADIR/$rel"
  done <<< "$LIST"
  echo "# file list — verify from inside $OUT with: sha256sum -c MANIFEST.txt"
  ( cd "$OUT" && sha256sum index.html && find assets -type f | sort | xargs sha256sum )
} > "$MAN"

echo "built: $OUT"
echo "files: $(find "$OUT" -type f | wc -l), size: $(du -sh "$OUT" | cut -f1)"
( cd "$OUT" && sha256sum -c MANIFEST.txt >/dev/null && echo "manifest verify: OK" )
