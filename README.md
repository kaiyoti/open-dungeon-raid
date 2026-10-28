# Open Dungeon Raid

A fan-made, from-scratch **reimplementation of Dungeon Raid** (the match-3 dungeon crawl:
trace chains of tiles, level your classes, survive) that keeps it running on modern
browsers and Android devices. C++ game core compiled to WebAssembly, a self-contained
HTML/JS page, and an optional Android WebView wrapper.

> ### Preservation notice
> This repository is a **game-preservation effort**: the original *Dungeon Raid* is no
> longer maintained or available, and this rebuild exists to keep the game playable on
> modern devices and browsers. It is an **unofficial fan project, not affiliated with**
> and not endorsed by the original authors. The original game's **art, sounds and fonts
> are the property of the original developer** and are included here **only** to keep the
> abandoned game playable; if you hold rights to the original game and want this
> redistribution stopped, **open an issue** and it will be removed. This repository's
> code is licensed under the **GNU General Public License v3 only (GPL-3.0-only)**, see
> [LICENSE](LICENSE) — that licence covers **this repository's code only**, never the
> original game's assets or fonts.

### Provenance

Open Dungeon Raid is a ground-up rebuild of **Dungeon Raid**, the match-3 RPG by
**Fireflame Games** (developer **Alex Kuptsov**) — the Android package is
`com.fireflame.dungeonraid` (verified from the original APK's manifest; *not*
`fireflame.dungeonraid`), `versionName 1.2.11`, `versionCode 14`. The original was
released on iOS in **November 2010** (TouchArcade: “Dungeon Raid was released more than
5 years ago in November of 2010”), received its last content update in May 2011, was
built for iOS 3.X, and was removed from the App Store with the **2017 32-bit cutoff**
(iOS 11; Dungeon Raid is on TouchArcade's “32-bit games that will die with iOS 11”
lists). The art and sounds bundled here were **extracted from that Android APK,
version 1.2.11 (versionCode 14)** — `aapt2 dump badging` on the APK reads
`package: name='com.fireflame.dungeonraid' versionCode='14' versionName='1.2.11'`.

## What is in here

| Path | What |
|---|---|
| `rebuild/` | the game itself: `src/` + `include/` (C++ core), `tests/` (C++ tests), `web/` (page, wasm build, node tests) |
| `assets/ripped/`, `assets/ripped-hd/` | the original game's art/sounds/font tables (1x) and the 4x HD atlases — preservation inclusion (see notice above) |
| `android/` | thin WebView wrapper that packages the page as an APK |
| `tools/` | build helpers: art dist builder, optional asset setup / HD atlas generation |
| `fonts/` | Della Respira (SIL OFL 1.1) — bundled fallback for the optional HD font step |
| `.github/workflows/pages.yml` | deploys the playable builds to GitHub Pages |

## Play it

* **Online:** <https://kaiyoti.github.io/open-dungeon-raid/> (Original) and
  <https://kaiyoti.github.io/open-dungeon-raid/hd/> (HD) — after the repository is on
  GitHub with *Settings → Pages → Source: GitHub Actions*.
* **Locally:** serve the repository root and open the art build:

```bash
python3 -m http.server 8000
# then open http://localhost:8000/rebuild/web/demo_art.html
```

## Prerequisites (versions this tree is verified with)

| Tool | Version |
|---|---|
| bash | 5.2 |
| g++ (C++17) | 13.3 (any g++ ≥ 10 / clang ≥ 12 should work) |
| Python | 3.12 (asset tools also need `numpy` + `pillow`) |
| Node.js | 22 (runs the web test suite) |
| Emscripten SDK | **3.1.74** (`rebuild/web/build.sh` pins this) |
| JDK | 17+ (verified with OpenJDK 21) |
| Android SDK | platform 34 + build-tools 34 (`apksigner`, `aapt2`), `ANDROID_HOME` set |

```bash
sudo apt-get install -y python3-numpy python3-pil   # for tools/setup_assets.sh + tools/hd/*
```

(On systems without apt, install `numpy` and `pillow` into a Python virtualenv and use
that environment's `python3` for the asset tools.)

## Build instructions (verified from a fresh clone)

### 1. Get the code

```bash
git clone https://github.com/kaiyoti/open-dungeon-raid.git
cd open-dungeon-raid
```

### 2. Build and test the C++ core

```bash
./rebuild/build.sh --test
```

Compiles every `rebuild/src/*.cpp` together with `rebuild/tests/sim.cpp` and runs the
simulation tests. (`./rebuild/build.sh` with no arguments just compiles every source
file; `./rebuild/build.sh --link` links everything with a dummy `main`.)

### 3. Build the web game

```bash
source /path/to/emsdk/emsdk_env.sh      # emsdk 3.1.74
./rebuild/web/build.sh --art            # -> rebuild/web/demo_art.html  (Original)
./rebuild/web/build.sh --hd             # -> rebuild/web/demo_art_hd.html (HD, 4x atlases)
```

Both targets rebuild the wasm (`rebuild/web/dr.js`) from `rebuild/src` + `rebuild/web/api.cpp`
first, then inline it into the page as one self-contained HTML file. The art atlases are
**not** embedded — `demo_art.html` fetches the original game's assets from `assets/ripped/`
(`demo_art_hd.html` from `assets/ripped-hd/`) relative to the page at runtime.

### 4. Run the tests

```bash
for t in rebuild/web/test_*.js; do node "$t" || exit 1; done
```

Node-only tests against the built `rebuild/web/dr.js` and `rebuild/web/template.html`
(board/rules, save & localStorage guards, menus, HUD, sound, boot order, …).
The internal capture-comparison and Chrome screenshot tests are not published here
(they drive harness scripts and fixtures that read from the original binary).

### 5. Set up your own assets (optional — assets already ship with this repo)

Everything needed to play is committed. These steps are only for refreshing the assets
from a Dungeon Raid APK that **you** own, or for the optional HD font generation:

```bash
./tools/setup_assets.sh /path/to/DungeonRaid-1.2.11.apk --force   # re-extract art/sounds/fonts
./tools/setup_assets.sh --fonts-only                              # optional: HD font atlases
```

The optional font step regenerates the HD `font*.png` atlases from a TTF:
* **MoolBoran** (`MOOLBOR.TTF`, the original body font): put it in `assets/fonts-private/`,
  or pass `--moolbor /path/to/MOOLBOR.TTF`, or (on Windows) install the optional feature
  *Settings → Apps → Optional features → "Sinhala Supplemental Fonts"* which provides
  `C:\Windows\Fonts\MOOLBOR.TTF`. **It is never downloaded by this repository**
  (Microsoft-licensed, not part of the game).
* **Without it**, the step falls back to the bundled **Della Respira** (`fonts/`, SIL OFL 1.1).

### 6. Regenerate the HD atlases locally (optional — shipped HD atlases already work)

The committed `assets/ripped-hd/*.png` were produced with the **4x-AnimeSharp** model.
To regenerate them yourself (model obtained from its official source):

```bash
mkdir -p ~/hd-tools/models
curl -L -o ~/hd-tools/models/4x-AnimeSharp.pth \
  https://huggingface.co/Kim2091/AnimeSharp/resolve/main/4x-AnimeSharp.pth
python3 -m venv ~/hd-tools/venv
~/hd-tools/venv/bin/pip install torch spandrel pillow numpy
~/hd-tools/venv/bin/python tools/hd/upscale.py animesharp \
  assets/ripped/tiles.png  assets/ripped-hd/tiles.png  --base
~/hd-tools/venv/bin/python tools/hd/upscale.py animesharp \
  assets/ripped/icons.png  assets/ripped-hd/icons.png  --base
~/hd-tools/venv/bin/python tools/hd/upscale.py animesharp \
  assets/ripped/menus.png  assets/ripped-hd/menus.png  --base
```

Alternative upscalers in the same tool: `--method esrgan` (Real-ESRGAN, official models
from <https://github.com/xinntao/Real-ESRGAN/releases>, e.g.
`RealESRGAN_x4plus_anime_6B.pth` → `~/hd-tools/models/`, needs `pip install basicsr realesrgan`)
and `--method xbrz` (needs a local `xbrzscale` binary at `~/hd-tools/bin/xbrz_raw`).
Font atlases are regenerated with `tools/hd/regen_fonts.py --write` (step 5).

Steps 5 and 6 rewrite tracked files under `assets/ripped-hd/` in your working tree
(that is the point — your local atlas build). Restore the shipped versions any time with:

```bash
git checkout -- assets/ripped-hd
```

### 7. Build the Android APK

```bash
./tools/make_final.sh                                  # -> dist/dungeon-raid-M11-<date>-<n>/
./android/build_apk.sh "$(ls -dt dist/dungeon-raid-M11-* | head -1)" --debug
```

Debug APK: `dist/apk/dungeon-raid-<version>-debug.apk` (debug-signed, WebView CDP enabled).

For a **release** APK, put *your own* keystore at `~/keys/dungeon-raid/release.jks`
(alias `dungeonraid`) — no keys live in this repository — or let the script create a fresh
one there on first run (the random password is stored beside it in `pass.txt`; keep both
private). Then:

```bash
./android/build_apk.sh "$(ls -dt dist/dungeon-raid-M11-* | head -1)"
```

writes `dist/apk/dungeon-raid-<version>.apk` + `.sha256` (signed with `apksigner` from
your Android build-tools). The APK bundles the art dist, uses the placeholder launcher
icon from `tools/make_placeholder_icon.py`, and derives `versionCode` from the dist
version (`M11-20261027-9` → `2026102709`) so every build is a proper Android update.
Add `--hd` after `./tools/make_final.sh --hd` to build the separate HD package
(`local.dungeonraid.rebuild.hd`) instead.

### 8. GitHub Pages

`.github/workflows/pages.yml` builds both pages and deploys them (`/` and `/hd/`) with
pinned action versions; set *Settings → Pages → Source: GitHub Actions*, push, done.

## Credits and licenses

* **Code in this repository** — GNU General Public License v3 **only**
  (SPDX-License-Identifier: **GPL-3.0-only**), Copyright (C) 2026 kaiyoti,
  see [LICENSE](LICENSE) (full, unmodified GPLv3 text).
* **Original *Dungeon Raid* game — code, art, sounds and font tables**
  (`assets/ripped/`, `assets/ripped-hd/`, `sprites.json`): © Fireflame Games / the
  original developer and its licensors. **Not covered by this repository's GPL licence** —
  these remain their owners' property and ship only under the preservation notice at the
  top of this file. Credit: *Alex Kuptsov / Fireflame Games, for the original Dungeon Raid.*
* **Della Respira** (`fonts/DellaRespira-Regular.ttf`) — © 2011–2012 Nathan Willis,
  SIL Open Font License 1.1 (`fonts/OFL.txt`). GPL-3.0-compatible.
* **Dwarven Stonecraft** (heading font, extracted from the original APK) — © 2000
  SDFonts (rzy/SD), distributed by its author as “100% Free”; no licence restrictions
  are stated, so it can accompany this GPL-3.0-only work. (Its licence file is not
  embedded in the font; distributors list it as free for personal and commercial use.)
* **Emscripten** — the wasm glue inside `rebuild/web/dr.js` comes from the emscripten
  runtime (MIT licence, GPL-3.0-compatible); the emsdk toolchain itself is not shipped.
* **Gradle wrapper** (`android/gradle/wrapper/gradle-wrapper.jar`) — Apache License 2.0,
  GPL-3.0-compatible. Other build-time dependencies fetched by Gradle (AndroidX WebKit,
  Apache-2.0) are not bundled either.
* **No JavaScript/npm libraries are bundled** — the web page is hand-written vanilla JS
  with no CDN or node_modules dependencies.
* **4x-AnimeSharp model** — by Kim2091 on Hugging Face (via OpenModelDB),
  CC-BY-NC-SA-4.0. **Not bundled** in this repository; it is only an optional local
  download used to regenerate the HD atlases (step 6). Its NonCommercial clause is one
  reason it must stay out of this GPL project.
* **Real-ESRGAN** — xinntao/Real-ESRGAN (official releases; code under BSD-3-Clause).
  Optional upscaler; model weights are **not bundled**.
* **xBRZ / `xbrzscale`** — command-line tool © 2020 Przemysław Grzywacz, GPL-3.0-or-later
  (wrapping Zenju's xBRZ algorithm). Optional upscaler used via a **local** binary; not
  bundled. GPL-3.0-compatible.
* **Fonts you supply yourself** (e.g. MoolBoran) remain under their own licences and are
  never redistributed by this repository.
* README structure inspired by
  [FuzionCD/dream-game-android](https://github.com/FuzionCD/dream-game-android)'s
  README — a separate fan project, format reference only, not involved with this project.
