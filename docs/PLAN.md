# Legend of Doom 3DS (tinaut1986 fork): action plan

Living document. Read it at the start of every session; keep it current as things
change (what goes where: the table in `CLAUDE.md`).

- **Goal:** Legend of Doom playable on **New 3DS and Old 3DS/2DS, with stereoscopic
  3D**. New 3DS at **60 fps**. Old 3DS/2DS at **30 fps minimum**, aiming for 60 too.
  The way there is moving the world rendering from the CPU to the GPU (PICA200),
  measured on hardware at every step, with debug tools, a console link and a release
  workflow on par with `../zelda-tmc-3ds`.
- **Starting point:** EstebanPdN's `legend-of-doom-3ds` v1.0 (forked 2026-10-12,
  `main` = `upstream/main` = `b1f6c09`). New 3DS family only. The published profile
  `hardware-hybrid` renders the world with the classic software renderer (swrenderer, true
  colour, on the main thread only; corrected 2026-10-13, see `docs/frame-budget.md`) at 320x192 by default
  (50-100% render scale) and lets the PICA200 scale the finished frame to 400x240;
  uncapped, Doom logic at 35 Hz. It already has dual-screen menus, a bottom-screen
  map and items, touch/C-Stick camera, save previews, a CIA updater with stable and
  experimental channels, and diagnostic captures (L+R+A/X/Y). The owner's reading
  (2026-10-12): almost everything runs on the CPU, it is not known to reach 60 fps
  even on the New 3DS, there is no stereo 3D, and the Old 3DS does not run it.

## Status (2026-10-12)

Only what no other place records. Bugs: the open GitHub issues. Tasks: the unticked
boxes below. History: `git log` and the decisions log.

**Release line:** `release/v0.2.1` (on origin). Published: **`v0.2.0`** (2026-10-14, **stable**,
the first: `main` merged and tagged), with the GPU world, stereo 3D, the NES bottom screen, the
languages, AUTO UPDATE and the memory fix; before it **`v0.1.0`** (2026-10-12, beta).

**Branches:**

| Branch | What | State |
|---|---|---|
| _(merged)_ | `perf/frame-parts`, `perf/static-thinkers`, `feat/overlay-ftp-button`, `feat/bottom-nes` (U4), `feat/stereo-3d` (P3) merged into `release/v0.1.1` 2026-10-14 after the owner's checks on the New 3DS (stereo depth, sprites, trees and weapon; the bottom screen; nothing static stuck; the FTP button; the game as usual); what each holds and what was measured: [`docs/handoff-2026-10-14.md`](handoff-2026-10-14.md). The local `test/stereo-bottom` is now redundant | |
| _(merged, to check)_ | `feat/ingame-spanish` (U5: the game's own text in Spanish), `fix/updater-channels` (the Beta channel sees stable releases; notes since the installed version; CHANGELOG: ALL), `chore/remove-render-option` (no RENDER option: no effect with the GPU world), `docs/readme-fork` and these notes merged into `release/v0.2.1` 2026-10-15 for the v0.2.1 beta, on the owner's go-ahead, before the console check | Owner: check them on the v0.2.1 beta, then the stable release |
| _(merged, to check)_ | `fix/toast-over-map` (#6), `fix/press-start-legible`, `fix/untranslated-texts` and these notes merged into `release/v0.2.2` 2026-10-15 for the v0.2.2 beta, on the owner's go-ahead, before the console check | Owner: check them on the v0.2.2 beta |
| `feat/gpu-light-sky` | P2.3: the GPU world lit as the swrenderer lights it (sector level, fake contrast, fading with depth, MAP01's outdoor distance fog) and the sky as a background cylinder; sprites keep their render style (translucent, additive, the hit flash's stencil; Ganon's invisibility). Built, not yet checked on the console | Owner: MAP01 outdoors (sky, fog far away), a dark dungeon room, hitting a monster, Ghini, a Wizzrobe, Ganon |
| _(merged, to check)_ | `feat/frames-option-fps-counter` (OPTIONS -> VIDEO -> FRAMES: AUTO 60 / LOCK 30 / NO SKIP; the top counter and the developer overlay show the same figure), `fix/overlay-redraw-cost` (the overlay redraws once a second, `mallinfo` every 4 s) and `fix/menu-top-black` (#8, #9: a menu over a GPU-world level keeps drawing the world dimmed by a GPU quad; the save picture is rendered by the CPU), plus `fix/build-ftp-login` and `fix/gpu-comment-contract-test`, merged into `release/v0.2.3` 2026-10-15 | Owner: FRAMES and the overlay checked on the New 3DS; #8 and #9 seen by Claude only (screenshot of the pause menu, a test save's picture): the owner to look |
| `test/2ds-memory` | P4.1 memory work for the 2DS, on top of `feat/old3ds-boot` (pushed; not for merging as it is: to be split into topics once it runs): ZScript compiled for this game only (`wadsrc/static/zscript.txt`, `lod_kept.zs`), the engine tolerant of the missing classes, map arrays built in place, a 10 MB pool for TArray's big blocks. Last install on the 2DS (before the pool's last build) stopped at the title with `operator new failure` | Next: install on the 2DS, read `fatal.log` and the new `pool=` figures in `startup.log` |
| `perf/bsp-distance-cull` | `walk_ms` and `capture_ms` in the perf recorder (inside `world_ms`), `--ftp` login (merged separately). The BSP pruning itself is **not** done: see the decisions log, 2026-10-15 | Parked: the walk is 1.7-3.3 ms of a frame |
| `feat/old3ds-boot` | P4.1 in progress (pushed, not merged; not yet checked on the New 3DS: the 804 MHz/L2 now come from `osSetSpeedupEnable` through `ptm:sysm`, not the exheader): one CIA for both models (no New 3DS-only SMDH flag; Old 3DS mode 80 MB; 12 MB of linear memory), map WADs stored in the pk3, a 5 MB map-load reserve. On the 2DS it gets 80 MB and the GPU, compiles ZScript, then a 1.3 MB lump fails to load on a fragmented heap (16 MB free) before the title | Next session: memory reduction first (decisions log, 2026-10-14) |
| _(merged)_ | `feat/stable-dev-ftp-banner` (stable builds: no DEBUG tab or developer overlay, SHOW FPS in VIDEO; the install-only FTP bar with its X), `feat/auto-update` (AUTO UPDATE: the offer at start, YES / NO, notes on the top screen; an installed update restarts the game; ':' and '/' in the NES font) and `feat/install-progress` (FTP install: bars for receiving and installing, relaunch countdown) merged into `release/v0.1.1` 2026-10-14 after the owner's checks on the New 3DS with a stable-mode build posing as 0.0.9 | |
| _(merged)_ | `feat/gpu-world-default` (the GPU draws the world in every build; stable builds cannot turn it off; owner 2026-10-14: never the CPU) and `docs/release-notes-v0.2.0` merged into `release/v0.1.1` 2026-10-14 after the owner saw it start on the GPU and approved the notes | |
| _(merged)_ | `fix/texture-memory` (#4: the out-of-memory close after the first dungeon; texture mipmaps skipped while `r_mipmap` is off, the `ibo_data` leak on every map load, lighter map loading, heap logs) merged into `release/v0.1.1` 2026-10-14 after the owner finished the first dungeon and went back to the overworld on the New 3DS with no close (MAP01 at 62.2 MB on both returns) | |
| _(merged)_ | `feat/gpu-world-spike` (P2.3 first step: walls and flats of the swrenderer's visible set drawn by the PICA200, `lod3ds_gpu_world`, debug builds, off by default; and the bottom map's flicker above 35 fps) merged into `release/v0.1.1` 2026-10-13 after the owner's look on the New 3DS ("60 fuera y 110 en mazmorra", no flicker) | |
| _(merged)_ | `docs/gpu-path` (P2.1 note, P2.2 decision, the one-shot hwrenderer probe for debug builds) merged into `release/v0.1.1` 2026-10-13 on the owner's go-ahead | |
| _(merged)_ | `feat/overlay-anywhere` (the developer overlay with SELECT also over the title and its menus, owner 2026-10-13) merged into `release/v0.1.1` 2026-10-13 on the owner's go-ahead after Claude saw it work on the New 3DS | |
| _(merged)_ | `fix/install-relaunch` (#1: the relaunch after `SITE INSTALL` waits 10 s, through libctru's chainloader) merged into `release/v0.1.1` 2026-10-13 after 6 installs in a row on the New 3DS with no loader dump | |
| _(merged)_ | `feat/gpu-2d` (U2) merged into `release/v0.1.1` 2026-10-13 after the owner's check on the New 3DS ("funciona"; gameplay still ~20 fps: the 3D view, P2) | |
| _(merged)_ | `feat/language` (U1 + U3, with `feat/native-menus`) merged into `release/v0.1.1` 2026-10-13 after the owner's check on the New 3DS ("todo ok") | |
| _(merged)_ | `fix/ftp-stop-freeze` (from another session, merged before its check): checked on the New 3DS 2026-10-13 with build `67b4f76`, FTP SERVER turned off and on several times, once with a client connected; each stop logged `ftp stop: done` in under 0.5 s | |
| _(none other open)_ | Merged into `release/v0.1.1` 2026-10-13 after the owner's checks on the New 3DS: `feat/console-link` (P1.4/P1.5; not yet tried: `touch` in gameplay, `console map`, the updater with the server on), `feat/perf-recorder` (P1.3), `fix/save-settings-on-quit` | |

**Priority** (2026-10-14, owner, later the same day): v0.2.0 published; next **P4 the 2DS**: boot it and aim for 60 fps
mono there (P4.1, P4.2), since CPU savings for the Old 3DS help everything; then P3.2 / P2.4
(stereo at 60 on the New 3DS) and #5 (title and menus at 35 fps). P2.3's parity (light, sky) for
cost along the way.



Tasks ticked [x] have been checked on the console by the owner; do not re-propose them.

## How work is tracked

- **This file** is the roadmap: phases, task specs with acceptance criteria,
  decisions, and a short Status (open branches, priority). It is the source of
  truth for "what next" and is what a new Claude session reads.
- **GitHub issues** on `tinaut1986/legend-of-doom-3ds` are for bugs found by playing:
  things with a repro, screenshots, a console model. Link the issue from the task
  here when a task spawns from one; do not duplicate specs into issues.
- Larger design work gets its own note in `docs/` and is linked from its task.

Task format: `- [ ] **ID** title`, then *Spec* (what) and *Done when* (verifiable
criteria). IDs are stable; never renumber.

---

## Known facts about the upstream code (verified 2026-10-12)

- History is regular commits (not squashed); tags `v0.31`, `v0.32`, `v0.8-E3`, `v1.0`,
  `v1.1` (`v1.1` is an updater test with fictional notes). The version string is
  `platform/3ds/version.txt` (`1.0`), passed to CMake as `LOD3DS_PORT_VERSION`.
- Build: `platform/3ds/build.sh [profile]` fetches the pinned dependencies
  (`dependencies.sh`), applies `platform/3ds/patches/`, builds host tools, the engine,
  pk3s, and packages `.3dsx`/`.cia`/SD zip/debug symbols with a `BUILD-MANIFEST.txt`.
  Default profile `hardware-safe` (CPU-only, SDL presenter); CI builds only that one.
- Identity: title ID `0x5A2D0`, product code `CTR-P-LDO3`, title "Legend of Doom 3DS"
  (`platform/3ds/cia/legend-of-doom-3ds.rsf`); data in `sdmc:/3ds/legend-of-doom/`
  (`i_specialpaths.cpp`, `diagnostics_3ds.cpp`, `updater.c`).
- Updater (`src/common/platform/3ds/update/`, doc `platform/3ds/UPDATER.md`): repository
  hard-coded as `UPDATE_REPOSITORY` (`EstebanPdN/legend-of-doom-3ds`); channels are
  GitHub's `prerelease` flag; checks the asset's SHA-256, its name
  (`legend-of-doom-3ds-v<ver>.cia`) and the installed title ID (hard-coded in
  `updater.c`); shows the whole release body word-wrapped to 42 columns.
- CI (`build-3ds.yml`) rejects tracked game data/binaries and the files `AGENTS.md`,
  `PROTOCOL.md`, `WORKFLOW.md`, `GITHUB_RELEASE_WORKFLOW.md` (upstream's own agent
  notes kept out of the repo). `CLAUDE.md` and `docs/PLAN.md` are not on that list.
- Rendering: GZDoom 4.7.1 carries the software/SoftPoly renderers
  (`src/common/rendering/polyrenderer`, `src/rendering/swrenderer`) and the hardware
  renderer abstraction (`src/common/rendering/hwrenderer`, with `gl`, `gles` and
  `vulkan` backends). Upstream's GPU attempt is NovaGL (GLES 1.x over citro3d, profiles
  `release`, `hardware-candidate`, `hardware-diagnostic`, many `novagl-*.patch`), not
  used by the published profile. No `gfxSet3D` / stereo anywhere in the 3DS code.
- New 3DS only by construction: the RSF asks for `SystemModeExt: 124MB`,
  `CpuSpeed: 804MHz` and core 2 (`CanAccessCore2`), and `hardware-hybrid` rasterises
  on CPU0 + CPU2. The Old 3DS has 268 MHz, two app-usable cores at best (core 1 is
  shared with the system) and 64 MB (up to 96 MB in another memory mode): memory is a
  risk to measure before anything else there (P4.1).
- Doom's game logic runs at 35 tics/s. Reaching 60 fps on screen means rendering
  interpolated frames between tics (GZDoom's uncapped mode, `cl_capfps 0`), paced to
  the 60 Hz LCD.
- Upstream's notes are in Spanish and versioned (`platform/3ds/FIXES-v0xx.es.md`,
  `CODE-REVIEW-v031.es.md`, `PERFORMANCE-AUDIT.es.md`, `docs/history/`): history, not
  current state. `platform/3ds/tools/summarize-telemetry.py` reads the existing telemetry.

## Reuse map from `../zelda-tmc-3ds`, `../sm-3ds` and `../mzm`

| Area | Source | Reuse |
|---|---|---|
| Version from git, upstream base shown next to ours, channel baked in | `../zelda-tmc-3ds/platform/3ds/tools/version.sh`, its `CMakeLists.txt` generated headers | P0.1, adapt to `build.sh` and `LOD3DS_PORT_VERSION` |
| Release CI (tag → CIA + QR, Beta/Release by reachability, notes block), compile-only CI | `../zelda-tmc-3ds/.github/workflows/build-release.yml`, `build-3ds.yml` | P0.1, keep upstream's manifest/checksum checks |
| Build assistant with LAN FTP upload | `../zelda-tmc-3ds/platform/3ds/tools/build_3ds.py` (from `../sm-3ds`) | P0.1 |
| DEBUG switch, SD log, perf recorder + analyser | `../zelda-tmc-3ds` P1, `../sm-3ds` `debug_log.c`, `perf_rec.c`, `tools/perf-csv/analyze.py` | P1, columns for SoftPoly/presenter |
| FTP server in the game, install + relaunch, remote link | `../zelda-tmc-3ds` `ftp_server_3ds.*`, `cia_install_3ds.c`, `tools/tmc-link/` | P1 |
| GPU renderer design, parity check against the software output, Old 3DS work | `../zelda-tmc-3ds` `docs/gpu-rasterizer-design.md`, `docs/renderer-gpu-gap.md`; `../mzm/docs/3ds-renderer-perf-plan.md` | Method; the renderer itself is new (3D, not tiles) |
| Stereo 3D (slider, per-eye views, 2D at screen depth) | `../sm-3ds/docs/stereo-design.md`, `../sm-3ds` PLAN Phase 3 | Design ideas |
| Perf method (measure on hardware, CSV, one change at a time) | `../mzm/docs/3ds-renderer-perf-plan.md`, `../sm-3ds` PLAN P2.x | Method |

---

## Phase 0: fork foundation

- [ ] **P0.1** Own the fork: versions, app identity, releases.
  *Spec:* version from git (`vX.Y.Z` tags only, `X.Y.Z-dev.N+hash` on release
  branches), `version.txt` → `upstream_version.txt` shown next to ours (title/info
  screen, manifest, dumps); own title ID `0x5A2D1` and data folder `sdmc:/3ds/Legend of Doom 3DS/` so it
  installs beside upstream's app (every hard-coded `legend-of-doom` path and the updater's title-ID
  check); updater pointed at this repository, aware of `-beta` / `-dev`, showing the
  `<!-- lod-notes -->` block; `build-release.yml` (tag → CIA + QR, Beta/Release by
  reachability from `main`, debug tools in betas, the published profile);
  `build-3ds.yml` also on `release/**`; build assistant with FTP; Build section of
  `CLAUDE.md` updated.
  *Done when:* the owner installs a CIA built from `release/v0.1.0` next to
  upstream's app, it uses its own data folder and shows `0.1.0-dev...` (upstream 1.0);
  a first beta tag publishes a release whose CIA the console's updater sees.
- [x] **P0.2** Issues enabled on `tinaut1986/legend-of-doom-3ds`.
  *Spec:* the fork has issues disabled (GitHub's default for forks). The owner
  enables them in Settings, or asks Claude to (`gh repo edit --enable-issues`).
  *Done when:* playtest bugs can be filed there.
- [ ] **P0.3** Baseline measurements.
  *Spec:* with the P1 perf recorder, record fixed spots (title, overworld start, a
  busy overworld screen, the first dungeon, a boss) on the owner's New 3DS with every
  shipped profile (`hardware-hybrid` at the default render scale and at 100%,
  `hardware-safe`), and whether each boots on the Old 3DS/2DS at all.
  *Done when:* a table below with fps (avg / 1% low) and CPU ms per frame split by
  logic, SoftPoly, presenter and audio.

## Phase 1: debug tools and the console link (debug builds only)

Specs are written when the phase starts, from `../zelda-tmc-3ds` P1 (same IDs order).

- [ ] **P1.1** DEBUG build switch and a DEBUG page in the bottom-screen menus.
- [ ] **P1.2** SD log (rotating, buffered/direct, settings and periodic perf lines).
- [x] **P1.3** Perf recorder (CSV per frame) + `tools/perf-csv/analyze.py`.
  *Spec:* `perf_3ds.cpp` (from TMC's `debug_perf_3ds`): one row per presented frame in RAM,
  written as `debug/perf-NNNN.csv` on stop; parts timed where the engine does them
  (logic tics, 3D view, SoftPoly raster wait, 2D, 3DS compositor, PICA200 present,
  limiter, bottom screen, the rest as `other`), game state, menu and canvas size; console
  command `lod3ds_perf start|stop|status`; `platform/3ds/tools/perf-analyze.py`;
  `lod-link.py perf SECONDS` records, fetches and summarises.
  *Done when:* recordings from the New 3DS split the frame by part (first ones in the
  Baseline section below).
- [ ] **P1.4** FTP server in the game; install a CIA over FTP and relaunch.
  *Spec:* `../mzm`'s server (one client, passive, SD root, login in
  `ftp_credentials_3ds.h`) behind OPTIONS -> DEVELOPER -> FTP SERVER (archived cvar
  `lod3ds_ftp_server`, debug builds), retrying while there is no Wi-Fi; `SITE INSTALL`
  installs a CIA of this title over the running game and jumps into it, with a warning
  toast on the touch screen; the socket service shared with the updater (`net_3ds.c`);
  stable builds only an install-only server when `dev-ftp.txt` exists;
  `./build_3ds.sh --install`.
  *Done when:* on the New 3DS, FileZilla/curl list and fetch the data folder while the
  game runs, and `./build_3ds.sh --install` replaces the running build, which comes back
  with the server on; the updater still checks for updates with the server on.
- [ ] **P1.5** Remote link (keys, touch, screenshot, state, memory by symbol) for
  Claude; `CLAUDE.md` "The console in the loop" written when it lands.
  *Spec:* `SITE` commands of `debug_link_3ds.h`: KEYS, STICK (Circle Pad / C-Stick),
  TOUCH, SHOT, DUMP, STATE (tic, game state, menu and item, map, player, fps), CONSOLE
  (GZDoom console command), CVAR, PEEK/POKE; buttons and taps reach the game as SDL
  events, sticks through the joystick poll; client `platform/3ds/tools/lod-link.py`.
  *Done when:* from the PC, `lod-link.py` opens the menu and walks it with `press`,
  moves the player with `stick`, warps with `console map`, and `shot` returns both
  screens as they look on the console.

## Phase U: menus, 2D and the bottom screen

From the owner (2026-10-13): menus nested inside menus just to tick an option, a pixel-art
look that does not have to mean poor design, the FPS counter tiny over a menu but half the
screen wide in gameplay, messages on the bottom screen as toasts, and everything on the
GPU. The first P1.3 recordings (Baseline below) say where the menu frames go.

- [x] **U1** Menus that are quick to use and look good.
  *Spec:* options without submenus to enter just to flip a value (what is changed is
  visible and changed in place, by button and by touch); drawn directly for the bottom
  LCD at its own resolution with integer-scaled pixel fonts, instead of the engine's menu
  rendered on the top canvas, pixel-diffed and rescaled on the CPU; while a menu is open
  over a paused game the 3D view is not rendered again every frame.
  *Done when:* the owner approves the look on the console, and a pause-menu recording
  shows no 3D view or routing cost (9.4 fps at render scale 1.0 before).
- [x] **U2** 2D and the bottom screen on the GPU.
  *Spec:* title picture, HUD, menus, FPS counter and the bottom-screen map/items drawn by
  the PICA200 (citro2d) instead of the SoftPoly raster and CPU loops; the FPS counter and
  other overlays at the LCD's native resolution whatever the render scale.
  *Done when:* recordings show the title's 70 ms and the bottom map's 34 ms gone, and the
  FPS counter is the same size in gameplay and menus.
  *Known (2026-10-13):* the gameplay FPS counter is drawn by `I_3DSComposeGameplayFrame`
  into the canvas at the render scale (e.g. 280x168 at 0.7, with smaller "compact" glyphs
  under 200 wide), which the PICA200 then stretches to 400x240, so it grows with a lower
  render scale; over a menu the canvas is 400x240 and it is drawn 1:1. The hybrid
  presenter (`I_PolyPresentDirect3DS`, `sdlglvideo.cpp`) draws with citro2d after the
  stretch, which is where native-resolution overlays belong; note its `C2D_Init(16)`
  object limit.

- [x] **U3** Language option: English and Spanish (castellano), the interface only.
  *Spec:* owner 2026-10-13: an option to pick the language, everything translated; for
  now only what the port draws (menus, options, dialogs, toasts, hints, the bottom
  screen, the updater), not the game's own text in the Legend of Doom pk3. Spanish
  letters the NES font lacks (Á É Í Ó Ú Ñ Ü ¿ ¡) drawn in its 8x8 style.
  *Done when:* the owner switches the language on the console and finds no English left
  in the interface in Spanish (and none Spanish in English).

- [x] **U4** The gameplay bottom screen like the NES Zelda's pause screen, with debug cheats.
  *Spec:* owner 2026-10-13: the MAP / ITEMS tabs look bolted on, their text scaled, the layout
  and style clash, and a touch on the map both zoomed it and moved the camera. One screen with
  no tabs, on black with the NES palette and font: INVENTORY with the B item's box and every
  item in one frame (a touch selects one), the map in a frame with its own zoom button, the
  rupee / key / bomb counters and -LIFE- with the hearts; every other touch moves the camera.
  Debug builds: GOD (the engine's god mode) and ALL (every item, the counters full, sixteen
  hearts; the triforce pieces left out, they drive the story).
  *Done when:* the owner finds it integrated on the console, the map zooms only with its
  button, and GOD / ALL work in a debug build and are absent from a stable one.

- [ ] **U5** The game's own text in Spanish too (U3 left it out).
  *Spec:* owner 2026-10-15: the whole game, not only the menus. The NPCs' dialogue (compiled
  ACS, through `Tr()` in the engine), pickup messages, obituaries, level names, the story and
  ending pages (Spanish variants drawn at build time), and the Spanish letters in the game's
  own fonts (`platform/3ds/tools/spanish-game.py`).
  *Done when:* in Spanish the owner meets no English in a playthrough's text (dialogue,
  pickups, story, ending), the accents read well in the dialogue box and the pickup line,
  the texts fit on screen, and English is unchanged.

## Phase 2: GPU world renderer, 60 fps on New 3DS

The base for everything else: 3D doubles the world drawing and the Old 3DS has a
third of the CPU, so neither is reachable with SoftPoly on the CPU.

- [ ] **P2.1** Where the frame goes today. Note: [`docs/frame-budget.md`](frame-budget.md)
  (first numbers 2026-10-13; a busy screen, a boss and the hwrenderer's scene walk still to record).
  *Spec:* from P0.3's recordings, split the New 3DS frame into game logic,
  BSP/visibility, SoftPoly raster (per core), presenter, audio, and idle; identify what
  stays on the CPU whatever the renderer (logic, BSP, ZScript).
  *Done when:* a note in `docs/` with those numbers and the CPU budget left for
  rendering at 60 fps (16.7 ms) and 30 fps.
- [x] **P2.2** Choose the GPU path. Draft: [`docs/gpu-renderer-design.md`](gpu-renderer-design.md)
  (measured 2026-10-13: the hwrenderer's CPU side is ~20 ms on MAP01; decided: (c), a renderer
  of our own fed by the swrenderer's visibility).
  *Spec:* compare, with measurements where possible: (a) upstream's NovaGL route
  (GZDoom's GLES renderer through a GLES 1.x layer); (b) a native citro3d backend for
  GZDoom's `hwrenderer` (vertex buffers, render state, PICA200 texture formats and
  sizes, the fog/light model the PICA200 can do with its TEV and fragment lighting);
  (c) anything else found. Criteria: fps headroom, fidelity to the software look
  (palette, light levels, fog), memory, how easily stereo comes out of it.
  *Done when:* a design note in `docs/` and the decision in the decisions log.
- [ ] **P2.3** GPU world renderer at parity.
  *Spec:* walls, flats, sprites, sky, decals, light levels and fades drawn on the GPU;
  2D (HUD, menus, automap) composed on top; a debug parity check against the software
  renderer, as in TMC's GPU CHECK.
  *Done when:* the P0.3 spots look the same as the software renderer (owner's eye and
  the parity check) and the GPU profile is the default in debug builds.
- [ ] **P2.4** 60 fps on New 3DS.
  *Spec:* interpolated frames between the 35 Hz tics, paced to the LCD's 60 Hz (no
  tearing, even frame times); the CPU work of the frame (logic, BSP, submit) kept under
  budget, moved to another core where it can be.
  *Done when:* the P0.3 spots hold 60 fps (1% low reported) on the New 3DS, with the
  perf recorder as evidence.

## Phase 3: stereoscopic 3D

- [ ] **P3.1** Stereo design.
  *Spec:* both eyes from the GPU renderer (two views with an eye offset, scaled by the
  3D slider; one shared visibility pass if the renderer allows it), weapon, HUD and
  menus at a fixed comfortable depth, the bottom screen unchanged; what happens on the
  Old 3DS and on a 2DS (no 3D: no extra cost).
  *Done when:* a design note in `docs/` and the decision logged.
- [ ] **P3.2** Stereo 3D on New 3DS.
  *Spec:* as P3.1; slider at zero costs nothing; an OPTIONS setting for depth.
  *Done when:* the owner sees correct depth on the console, and the P0.3 spots keep the
  P2.4 target with 3D on (60 fps on the New 3DS, owner 2026-10-12).

## Phase 4: Old 3DS / 2DS

- [ ] **P4.1** Boots on the Old 3DS.
  *Spec:* memory first: what the game needs (engine, pk3s, textures, audio) against the
  Old 3DS memory modes; RSF and code that assume the New 3DS (124 MB, 804 MHz, core 2)
  made conditional; one CIA for both models.
  *Done when:* it boots to the title and plays the first screens on the owner's 2DS,
  memory headroom recorded.
- [ ] **P4.2** 30 fps minimum on the Old 3DS.
  *Done when:* the P0.3 spots never drop under 30 fps (1% low) on the 2DS, steady pacing.
- [ ] **P4.3** 60 fps on the Old 3DS (aim).
  *Done when:* the P0.3 spots hold 60 fps on the 2DS, or the decisions log says what
  stops it and the best steady rate reached.
- [ ] **P4.4** Stereo 3D on Old 3DS (non-2DS models), at the P4.2 minimum at least.

## Baseline measurements (P0.3)

Partial, from the P1.3 recorder (`lod-link.py perf 10`) on the owner's New 3DS,
2026-10-13, build `edaf44ee684b` (`hardware-hybrid`, debug), render scale 1.0
(canvas 400x240; the default is 0.7). Milliseconds per frame, mean.

| Spot | fps (1% low) | logic | 3D view | raster wait | 2D | compose | present | bottom | other |
|---|---|---|---|---|---|---|---|---|---|
| Title screen | 13.2 (12.8) | 0.2 | 0 | 70.4 | 0.8 | 0 | 2.0 | 1.4 | 1.2 |
| MAP01, standing | 14.7 (7.9) | 9.2 | 27.3 | 11.8 | 0.8 | 0.5 | 2.2 | 14.7 (34 when redrawn) | 1.5 |
| MAP01, pause menu open | 9.4 (9.1) | 1.2 | 24.5 | 40.7 | 0.8 | 21.6 | 1.8 | 0 | 15.3 |

Readings: the title's 70 ms is 2D only (the title picture through the SoftPoly raster);
a paused game still renders the 3D view every frame; routing the menu to the bottom LCD
(pixel diff of the 400x240 canvas, copies, CPU scaling) costs ~37 ms; the bottom-screen
map costs 34 ms per redraw. Still to record: the default render scale, the first
dungeon, a boss, `hardware-safe`, the Old 3DS.

## Decisions log

- **2026-10-12** Fork set up with the workflow of `../zelda-tmc-3ds`: `CLAUDE.md`,
  this plan, release notes per tag, `main` stable / `release/vX.Y.Z` / topic branches.
  Own version line `vX.Y.Z` from `v0.1.0`; upstream's two-component and `-E` tags stay
  but are never ours. `upstream` push URL set to `DISABLED`, `gh` default set to the
  fork.
- **2026-10-12** Goal set by the owner: New 3DS and Old 3DS/2DS, stereo 3D; 60 fps on
  New 3DS, 30 minimum on Old 3DS aiming for 60. Hence the GPU world renderer (P2) comes
  before 3D and Old 3DS: SoftPoly on the CPU cannot pay for two eyes or a 268 MHz CPU.
- **2026-10-12** Owner: the New 3DS target of 60 fps holds with 3D on too. App identity
  for P0.1: title ID `0x5A2D1` (upstream `0x5A2D0`), data folder
  `sdmc:/3ds/Legend of Doom 3DS/` (upstream `sdmc:/3ds/legend-of-doom/`).
- **2026-10-12** P0.1 choices: version, channel, debug switch and build ID go into a
  generated `lod3ds_version.h` (only `diagnostics_3ds.cpp`, `updater.c`, `i_main.cpp`
  include it) instead of compile definitions on the whole `zdoom` target; upstream's
  build ID (a hash of the source tree) was one, so every edit rebuilt the engine. The
  SD folder and title ID live in `app_paths_3ds.h`. The NovaGL diagnostic patches keep
  upstream's folder (profiles unused, patches validated by hunk size). The updater's
  "Experimental" channel is called "Beta" and follows GitHub pre-releases. Releases
  publish the CIA (+ debug symbols, manifest, checksums, QR), not the 3DSX/SD zip: the
  updater installs CIAs only. The HOME-menu name is "Legend of Doom 3DS*" (owner,
  2026-10-12: same name for now, with a mark to tell it from upstream's app); the
  SMDH long description carries the version.
- **2026-10-12** P0.2: issues enabled on the fork (`gh repo edit --enable-issues`, owner's go).
- **2026-10-12** Owner checked the P0.1 dev CIA on the New 3DS (192.168.1.141, ftpd):
  installs as "Legend of Doom 3DS*" next to upstream's app, creates its own folder,
  shows the version and the BETA channel. P0.1 stays open until the updater sees the
  first published beta.
- **2026-10-12** First beta `v0.1.0` tagged on `release/v0.1.0` (merge `d715fc0`) and
  published by the release workflow; the release branch renamed to `release/v0.1.1`.
  P0.1 is done once the console's updater (BETA channel) lists `v0.1.0`.
- **2026-10-12** Release-notes marker `<!-- lod-notes -->` (TMC uses `tmc-notes`).
- **2026-10-13** Owner: settings were lost on closing; `fix/save-settings-on-quit` writes
  the config when leaving the option menus (upstream's fast QUIT GAME skips the final
  save on purpose, FIXES-v035, and HOME -> Close never reaches it).
- **2026-10-13** Priority set with the owner after the first recordings: U1 menus, then
  U2 2D/bottom screen on the GPU, then P2. The menu and 2D costs (title 70 ms of 2D,
  pause menu re-rendering the 3D view plus ~37 ms of routing, 34 ms per bottom-map
  redraw) are large, contained wins; the 3D view is ~40 ms of the gameplay frame and
  needs the bigger P2 work.
- **2026-10-13** U1 look, chosen with the owner from mockups: the game's own NES font
  (SMALLFNT lump, decoded at run time) at integer scales, x2 for the short list menus
  (main, pause, skill), x1 with 20-pixel rows for everything else (owner: "same font as
  the rest, x1" for UPDATE and the others); NES palette, blue selection box, heart cursor,
  a hint bar at the bottom; OPTIONS as one screen with four tabs (VIDEO, SOUND, CONTROL,
  DEBUG; L/R or a tap) instead of a root menu with submenus; sliders as ten cells in half
  steps; messages as toasts on the touch screen, not over the game. Menus drawn natively:
  every text list menu, the four option tabs, save/load, yes/no boxes, UPDATE (and its
  changelog on top) and CONTROLS; only the text keyboard keeps the old route. The top
  screen keeps the menu's first frame (dimmed) and D_Display renders nothing meanwhile.
- **2026-10-13** U3: the port's text goes through `Tr("ENGLISH")`, which looks up
  `LOD_ENGLISH` (letters and digits joined by `_`) in `language.csv`, added to the game
  pk3 by the game patch; English is the fallback, so a missing row never shows a blank.
  `language` = `auto` follows the 3DS system language (CFGU; Spanish or else English).
  Spanish capitals keep the full height of the NES font with the accent drawn above the
  cell (the line spacing has the room); a squeezed letter read as lower case. The game's
  own text (Legend of Doom's dialogue and items) stays English for now (owner).
- **2026-10-13** Owner moved P1.4/P1.5 (console link) ahead of the menu/HUD look work,
  so later UI work can be checked from the PC. Ported from `../mzm` (its server has the
  login and the install-only mode). Differences: the FTP thread runs one priority step
  *above* the game thread on core 0, as the updater's worker does (GZDoom's main thread
  stays runnable between frames and starved lower threads); remote buttons and taps are
  pushed as SDL events and sticks overridden in the joystick poll, instead of OR-ing HID
  masks, because input reaches GZDoom through SDL; no save states (GZDoom saves through
  `SITE CONSOLE save/load`), and CONSOLE/CVAR added; the relaunch after `SITE INSTALL`
  saves the config itself before the APT jump: once the jump is under way the app is
  closed before the engine's final save (seen on the console: FTP SERVER came back off).
- **2026-10-13** U2 design (`feat/gpu-2d`, `src/common/platform/3ds/gpu_3ds.cpp`): one citro3d
  context with its own small shader replaces the citro2d presenter. The engine's F2DDrawer is
  recorded into GPU draws at the LCD's 400x240 (SoftPoly's 2D shading reproduced in the TEV:
  texture modes, vertex colour, overlay colour, render styles as GPU blending); a drawer using
  what the GPU path does not do (shape buffers, burn, desaturation, special colormaps) and the
  frames whose 2D must be in the canvas pixels (a native menu's first, kept frame; the keyboard
  route; the wipe's new screen) still go through SoftPoly. Engine textures get a PICA copy owned
  by their PolyHardwareTexture. The bottom screen keeps its CPU drawing code but into a buffer
  the GPU copies to its target only when it changed (gameplay redraws when a state signature
  changes, not every 125 ms), and the map's terrain, lines and arrow are GPU triangles every
  frame (also the top automap). A/B switch: cvar `lod3ds_gpu_2d`. Measured on the New 3DS:
  title 13.2 → 35 fps (2D 70 ms → 0.01 ms); MAP01 at render scale 1.0 bottom screen 14.7 ms
  (34 per redraw) → 1.9 ms mean, 27.7 fps, 1% low 23.6.
- **2026-10-13** Lessons from the console while doing U2 (each one hung or crashed it): a
  citro3d frame with no draw is never submitted and the next `C3D_FrameBegin` waits forever; a
  GX transfer inside a frame runs alongside that frame's drawing (libctru starts up to three GX
  commands at once) and left the queue waiting forever, so the bottom layer is copied before
  `C3D_FrameBegin`; the CPU cannot read or write VRAM (data abort); a recorded draw must not
  point at a texture the engine can free before the present.
- **2026-10-13** The relaunch after `SITE INSTALL` (#1) waits 10 s before jumping. The loader
  dumps were all `0xD86007F3` (kernel out of memory) while allocating our title's code, with the
  new build already running: a second load of the title. Measured on the New 3DS: jumping at
  once faulted in 3 installs of 4 with either jump (by hand, or libctru's chainloader at exit);
  jumping with no install before (6) or after installing without jumping (3) never did; installing
  and jumping 10 s later never did (4, then 6 with the final build). The cause inside the system
  (presumably the HOME menu still handling the updated title when the jump arrives) is not
  known. The chainloader stays, as in `../sm-3ds`.
- **2026-10-13** P2.2 decided by the owner: **(c)**, a GPU world renderer of our own, fed by the
  swrenderer's opaque pass (its BSP walk, 1D occlusion and MAP01 distance cull) with each map's
  geometry built at load and draws batched by texture and light level
  (`docs/gpu-renderer-design.md`). GZDoom's hwrenderer was measured with its draws dropped:
  ~20 ms of CPU a frame on MAP01 even culled at the fog distance (BSP 11 ms, angle clipper 11 ms),
  against 7 ms for the swrenderer's whole scene setup and ~9.5 ms left for 60 fps. NovaGL (a) was
  already ruled out by upstream's history. The poly backend's drawers crash on the 3DS in mode 4
  (not investigated, unused by (c)). First step: a timed spike on MAP01 (walls and flats from
  load-time buffers, the swrenderer's visible set, no lighting).
- **2026-10-13** Priority set by the owner after the GPU world spike (60 fps on MAP01): stereo 3D
  (P3) next, then the 2DS (P4), before finishing P2.3's parity (light, sky, sprites). Stereo comes
  almost for free from the GPU world (two submissions of one visible set).
- **2026-10-14** P2.4 measured on the New 3DS with the GPU world: GZDoom's tic stability
  busy-wait (~3 ms a frame) is off on the 3DS (`r_ticstability 0`), and things that cannot change
  by themselves are no longer ticked (MAP01's ~490 bushes, spawn spots, teleport destinations):
  logic 7.35 -> ~5 ms a tic; with the walk trimmed and no canvas copy MAP01 runs at ~95 fps mono
  and ~64 stereo (GPU-bound). Details and the next steps: `docs/handoff-2026-10-14.md`.
- **2026-10-14** Memory before speed where they compete: the Old 3DS/2DS has at most 96 MB for
  the application, so every byte saved now is headroom for P4. The close after the first dungeon
  was a fragmented heap (a 2.7 MB request failing with 23 MB free), fed by two things: GZDoom
  4.7.1's `CreateVBO` never clearing `ibo_data` (every map load, savegames included, appended
  ~400 KB for MAP01) and the true-colour textures building a mipmap chain that `r_mipmap 0` (forced
  on the 3DS) never uses. Large transient blocks are the risk, so map loading now avoids them
  (one TEXTMAP copy, parsed arrays sized once). Still open: MAP01 holds its 2.5 MB of sides twice
  while loading (`ParsedSides` and `Level->sides`), and `CreateVBO`'s vertex data is built for the
  swrenderer too; whether anything on this path reads it is not checked yet.
- **2026-10-14** The owner reordered: the 2DS (P4) before stereo at 60 fps, aiming for 60 fps
  mono on the Old 3DS/2DS, because work that makes the CPU side cheaper helps every model. The
  world is drawn by the GPU in every build from now on (no CPU fallback in stable builds), and
  this line is published as v0.2.0, the first stable release (owner's call; minor bump for the
  GPU world, stereo 3D and the new bottom screen). Stereo on the New 3DS is GPU-bound (~64 fps),
  so 2DS work will not raise it by itself.
- **2026-10-14** P4.1 on the owner's 2DS. The HOME menu needs: no New 3DS-only flag in the SMDH
  (`cia-memory.py` used to set it), Old 3DS mode 80 MB (96 MB crashed HOME), and no `CpuSpeed` /
  `EnableL2Cache` / `CanAccessCore2` in the exheader (with them the 2DS gave the title 64 MB);
  `SystemModeExt: 124MB` may stay. The game then gets 80 MB, of which ~55 MB of heap: ZScript's
  compile peaks at ~45 MB and leaves the heap too fragmented for the next 1-2 MB block (MAP01's
  TEXTMAP, a 1.3 MB lump) with 16 MB free. Reserves only move the failure; the owner chose to
  **reduce the memory the game needs first** (next session): measure what holds the heap after
  the compile and in MAP01, then compile only the actors this game uses (gzdoom.pk3 compiles
  Doom, Heretic, Hexen, Strife and Chex), then 8-bit textures, the doubled sides, caches.
- **2026-10-15** ZScript for the 2DS: `gzdoom.pk3` compiles Doom, Heretic, Hexen, Strife and Chex;
  this game needs only Doom's base. Dropping 136 includes (`zscript.txt`) cut the compile's heap
  peak from ~45 MB to ~37 MB, but the files cannot just go: the kept ZScript and the mod name some
  of their classes (`BossSpot`, `BloodySkull`, `A_AlertMonsters`, `ArtiFly`, `ArtiPoisonBag1-3`,
  `Coin`; copied or stubbed in `lod_kept.zs`), and the engine needs to tolerate unknown classes in
  the spawn / conversation id maps, decal generators, key groups and bot weapons. A static check by
  class name missed all of it; what finds the rest is the compiler's own messages, which the 3DS
  has no console for, so `fatal.log` now carries them (`ScriptErrorSummary`).
- **2026-10-15** The 2DS heap fails on **fragmentation**, not on size: a 0.5 MB request failed with
  9-11 MB free in pieces. What helped, in order: building the level's sides / sectors / vertices in
  place instead of a second copy (`udmf.cpp`), reserving the big arrays while the heap the previous
  map freed is whole, and a 10 MB pool for TArray blocks of 192 KB or more (`m_alloc.cpp`,
  `M_ReallocBig`). The pool and the 5 MB map-load reserve together may leave the heap too small at
  the title: measure with `pool=` in `startup.log` before keeping both.
- **2026-10-15** Frame time on the New 3DS at MAP01, overlay closed (`perf-analyze`, debug build):
  looking at the sea 110 fps (world 3.3 ms), looking inland 70 fps (world 5.9: BSP walk 3.3, GPU
  hand-over 1.0; present 3.5; logic 2.4). Nothing dominates, so no pruning of the BSP is worth it
  alone; and a node-level distance prune is not safe as it stands (a far subtree can still own the
  columns that keep the sky out: `FarClipLine` exists for that). With FRAMES = AUTO both spots hold
  60.00 fps, 1% low 60.
- **2026-10-15** The "bottom-screen redraw spikes" first read from the recorder were the developer
  overlay (CPU drawing 8 times a second, 24 ms each, 15 of them one `mallinfo()` call on a
  fragmented heap), not the game's NES screen (`bottom_ms` 0.5 ms with the overlay closed). The
  pause and title menus run at 35 fps because the loop waits for the next tic when nothing moves
  (#5); that is idle time, not work.
- **2026-10-15** #8 and #9 had one cause: with the GPU world the canvas has no world in it. A
  native menu over a level now keeps rendering it (about 3 ms) and dims it with a GPU quad; the
  save and load menus, which draw their own picture on the top canvas, keep the frozen route. The
  save picture is rendered with the GPU world suspended.
