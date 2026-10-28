#!/usr/bin/env bash
# Build + sign the APK of the WebView wrapper.
#   android/build_apk.sh <dist-folder>     (e.g. dist/dungeon-raid-M11-20261026-3 or a bare name)
#   android/build_apk.sh <dist-folder> --debug   (U4b: debug build, debug-signed, CDP on;
#     writes dist/apk/dungeon-raid-<version>-debug.apk; release path unchanged)
#   android/build_apk.sh <dist-folder> --hd      (X3/M14: HD side variant — applicationId
#     local.dungeonraid.rebuild.hd, label "Open Dungeon Raid HD" (U14 rename); pairs with
#     tools/make_final.sh --hd; same icon, installs next to the normal rebuild)
# Does: copy <dist>/index.html + <dist>/assets into app/src/main/assets/, copy the ORIGINAL
# game icon (extracted/res/drawable-*-v4/icon.png) byte-for-byte into mipmap-* (Master change
# 2026-10-26), gradle assembleRelease (ONE heavy job: jvmargs/parallel come from
# gradle.properties, --no-daemon), apksigner sign with ~/keys/dungeon-raid/release.jks
# (created here if missing; random password in pass.txt), then write
# dist/apk/dungeon-raid-<dist-version>.apk + .sha256 and verify signature + icon bytes.
# versionCode (U9) = dist date*100 + build number (M11-20261027-9 -> 2026102709) — increases
# every build so the package manager treats each sideload as an update, never a downgrade.
# NEVER upload/publish the APK (it bundles ripped art + sounds).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP="$ROOT/android"
# U4b: --debug anywhere in the args selects the debug variant (debug-signed, BuildConfig.DEBUG
# -> WebView.setWebContentsDebuggingEnabled(true)); the dist folder is the first non-flag arg.
# X3/M14: --hd selects the HD applicationId + label (gradle -Phd=true).
DIST_ARG=""; DEBUG=0; HD=0
for A in "$@"; do
  case "$A" in
    --debug) DEBUG=1 ;;
    --hd) HD=1 ;;
    *) [ -z "$DIST_ARG" ] && DIST_ARG="$A" ;;
  esac
done
[ -n "$DIST_ARG" ] || { echo "usage: $0 <dist-folder> [--debug] [--hd]" >&2; exit 2; }

# ---- dist folder -------------------------------------------------------------------------
if [ -d "$DIST_ARG" ]; then DIST="$(cd "$DIST_ARG" && pwd)"
elif [ -d "$ROOT/dist/$DIST_ARG" ]; then DIST="$ROOT/dist/$DIST_ARG"
else echo "FATAL: dist folder not found: $DIST_ARG" >&2; exit 1; fi
[ -f "$DIST/index.html" ] && [ -d "$DIST/assets" ] \
  || { echo "FATAL: $DIST lacks index.html + assets/" >&2; exit 1; }
VERSION="$(basename "$DIST")"; VERSION="${VERSION#dungeon-raid-}"
echo "dist   : $DIST (version $VERSION)"

# ---- versionCode: MUST increase with every build (U9) -------------------------------------
# Derived from the dist name: M11-<YYYYMMDD>-<n> / HD-<YYYYMMDD>-<n> -> YYYYMMDD*100+n
# (M11-20261027-9 -> 2026102709). Monotonic in (date, n); both families are separate
# applicationIds so they may share the number space. n>99 would collide with the next
# day's numbers -> refuse instead of shipping a non-monotonic versionCode. Unrecognised
# names (hand-made dist folders) stay versionCode 1 = never-ship dev build.
if [[ "$VERSION" =~ ^(M11|HD)-([0-9]{8})-([0-9]+)$ ]]; then
  N="${BASH_REMATCH[3]}"
  if [ "$((10#$N))" -gt 99 ]; then
    echo "FATAL: dist build number $N > 99 — bump the versionCode scheme in android/build_apk.sh first" >&2
    exit 1
  fi
  VCODE="$((10#${BASH_REMATCH[2]} * 100 + 10#$N))"
else
  VCODE=1
fi
echo "versionCode: $VCODE (versionName: $VERSION)"

# ---- toolchain (U1) ----------------------------------------------------------------------
# shellcheck disable=SC1091
# Optional machine env (this host): public builders skip it and need ANDROID_HOME/PATH set
# themselves (platform-tools, build-tools for apksigner — see README).
[ -f "$HOME/android/env.sh" ] && source "$HOME/android/env.sh"

# ---- one heavy job host-wide: refuse to start gradle without 4 GB free -------------------
AVAIL="$(free -m | awk '/^Mem:/{print $7}')"
[ "$AVAIL" -ge 4000 ] || { echo "FATAL: available ${AVAIL}MB < 4000 — not starting gradle" >&2; exit 1; }

# ---- bundle the dist + the ORIGINAL game icon, byte-for-byte (Master change 2026-10-26) ---
rm -rf "$APP/app/src/main/assets"
mkdir -p "$APP/app/src/main/assets"
cp "$DIST/index.html" "$APP/app/src/main/assets/index.html"
cp -r "$DIST/assets" "$APP/app/src/main/assets/assets"
du -sh "$APP/app/src/main/assets"

# Launcher icon: PREFER the original game icon (extracted/ is a pristine copy of the APK:
# res/drawable-{ldpi,mdpi,hdpi,xhdpi}-v4/icon.png = 36/48/72/96 px, manifest icon
# @0x7f020004). Copied UNCHANGED into mipmap-*; no adaptive icon, no rescaling, no crop.
# Public builds (no extracted/ in the tree) keep the committed placeholder icons instead
# (tools/make_placeholder_icon.py) — the icon proof below only runs for the original icons.
# (anything generated here stays out of any public commit — android/.gitignore covers it).
HAVE_ORIG_ICON=0
if [ -f "$ROOT/extracted/res/drawable-xhdpi-v4/icon.png" ]; then
  HAVE_ORIG_ICON=1
  rm -rf "$APP/app/src/main/res"/mipmap-*
  for D in ldpi mdpi hdpi xhdpi; do
    mkdir -p "$APP/app/src/main/res/mipmap-$D"
    cp "$ROOT/extracted/res/drawable-$D-v4/icon.png" \
       "$APP/app/src/main/res/mipmap-$D/ic_launcher.png"
  done
  ( cd "$APP/app/src/main/res" && sha256sum mipmap-*/ic_launcher.png )
else
  echo "icon    : no extracted/ original icon — using the committed placeholder mipmap-*"
fi

# ---- keystore (private, ~/keys/dungeon-raid — never in the project or dist) --------------
KEYS="$HOME/keys/dungeon-raid"
mkdir -p "$KEYS"; chmod 700 "$KEYS"
if [ ! -f "$KEYS/release.jks" ]; then
  [ -f "$KEYS/pass.txt" ] || ( umask 077; openssl rand -hex 16 > "$KEYS/pass.txt" )
  chmod 600 "$KEYS/pass.txt"
  PASS="$(head -n1 "$KEYS/pass.txt")"
  keytool -genkeypair -noprompt -keystore "$KEYS/release.jks" -alias dungeonraid \
    -keyalg RSA -keysize 2048 -validity 10000 \
    -storepass "$PASS" -keypass "$PASS" \
    -dname "CN=Dungeon Raid (rebuild), OU=Private sideload, O=Local, L=Home, ST=NA, C=US"
  echo "created $KEYS/release.jks"
fi
[ -f "$KEYS/pass.txt" ] || { echo "FATAL: $KEYS/pass.txt missing" >&2; exit 1; }
chmod 600 "$KEYS/pass.txt"
# apksigner reads --ks-pass and --key-pass from the SAME file: spec as one stream:
# line 1 = keystore password, line 2 = private-key password (identical here, both set from
# this file by the keytool call above). Normalize: trailing newline + two identical lines.
if [ -n "$(tail -c1 "$KEYS/pass.txt")" ]; then echo >> "$KEYS/pass.txt"; fi
if [ "$(wc -l < "$KEYS/pass.txt")" -eq 1 ]; then head -n1 "$KEYS/pass.txt" >> "$KEYS/pass.txt"; fi
[ "$(wc -l < "$KEYS/pass.txt")" -ge 2 ] \
  || { echo "FATAL: $KEYS/pass.txt must contain the password on two lines" >&2; exit 1; }

# ---- gradle assemble (heavy: no parallel, no daemon, 1.5 GB heap) ----------------------
cd "$APP"
[ -x ./gradlew ] || { echo "FATAL: gradlew missing in $APP" >&2; exit 1; }
if [ "$DEBUG" = 1 ]; then
  GTASK=assembleDebug
  LOGD="$ROOT/tasks/U4b"
  APK_IN="$APP/app/build/outputs/apk/debug/app-debug.apk"   # AGP debug-signs this itself
else
  GTASK=assembleRelease
  LOGD="$ROOT/tasks/U2"
  APK_IN="$APP/app/build/outputs/apk/release/app-release-unsigned.apk"
fi
mkdir -p "$LOGD"
echo "=== gradle $GTASK (available was ${AVAIL}MB$( [ "$HD" = 1 ] && echo ', HD variant' )) ==="
GRADLE_ARGS=(--no-daemon --console=plain -PdistVersion="$VERSION" -PversionCode="$VCODE")
[ "$HD" = 1 ] && GRADLE_ARGS+=(-Phd=true)   # X3/M14: applicationId .hd + HD label
./gradlew "${GRADLE_ARGS[@]}" "$GTASK" \
  > "$LOGD/gradle.log" 2>&1 \
  || { echo "FATAL: gradle failed — tail of $LOGD/gradle.log:" >&2; \
       tail -40 "$LOGD/gradle.log" >&2; exit 1; }
tail -3 "$LOGD/gradle.log"

[ -f "$APK_IN" ] || { echo "FATAL: $APK_IN not produced" >&2; exit 1; }

# ---- sign (release only) + checksum + verify ---------------------------------------------
mkdir -p "$ROOT/dist/apk"
if [ "$DEBUG" = 1 ]; then
  OUT="$ROOT/dist/apk/dungeon-raid-$VERSION-debug.apk"
  cp "$APK_IN" "$OUT"          # already signed with AGP's debug key (debug-signed is fine)
else
  OUT="$ROOT/dist/apk/dungeon-raid-$VERSION.apk"
  apksigner sign --ks "$KEYS/release.jks" --ks-key-alias dungeonraid \
    --ks-pass "file:$KEYS/pass.txt" --key-pass "file:$KEYS/pass.txt" \
    --out "$OUT" "$APK_IN"
fi
( cd "$(dirname "$OUT")" && sha256sum "$(basename "$OUT")" > "$(basename "$OUT").sha256" )

echo "=== apksigner verify --print-certs ==="
apksigner verify --print-certs "$OUT" | tee "$LOGD/apksigner_verify.txt"
echo "=== aapt2 dump badging (head) ==="
aapt2 dump badging "$OUT" > "$LOGD/badging.txt"
head -8 "$LOGD/badging.txt" | tee "$LOGD/badging_head.txt"

# ---- icon proof (original icons only): bytes inside OUR apk == bytes of the original icon ----
if [ "$HAVE_ORIG_ICON" = 1 ]; then
echo "=== icon sha256: apk entry vs original apk (unzip -p) ==="
: > "$LOGD/icon_sha256.txt"
for D in ldpi mdpi hdpi xhdpi; do
  IN_APK="$(unzip -l "$OUT" | awk -v p="mipmap-$D-v4/" 'index($4,p) && index($4,"ic_launcher"){print $4; exit}')"
  [ -n "$IN_APK" ] || IN_APK="$(unzip -l "$OUT" | awk -v p="mipmap-$D/" 'index($4,p) && index($4,"ic_launcher"){print $4; exit}')"
  [ -n "$IN_APK" ] || { echo "FATAL: ic_launcher for $D not found in $OUT" >&2; exit 1; }
  A="$(unzip -p "$OUT" "$IN_APK" | sha256sum | cut -d' ' -f1)"
  B="$(unzip -p "$ROOT/DungeonRaid_1.2.11_modern.apk" "res/drawable-$D-v4/icon.png" | sha256sum | cut -d' ' -f1)"
  ST=OK; [ "$A" = "$B" ] || ST=MISMATCH
  printf '%s\t%s\t%s\t%s\n' "$D" "$IN_APK" "$ST" "$A" | tee -a "$LOGD/icon_sha256.txt"
  [ "$ST" = OK ] || { echo "FATAL: $D icon bytes differ from the original" >&2; exit 1; }
done
fi

echo "=== done ==="
ls -l "$OUT" "$OUT.sha256"
cat "$OUT.sha256"
