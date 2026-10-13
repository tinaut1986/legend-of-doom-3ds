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
  `hardware-hybrid` renders the world with SoftPoly on CPU0/CPU2 at 320x192 by default
  (50-100% render scale) and lets the PICA200 scale the finished frame to 400x240;
  uncapped, Doom logic at 35 Hz. It already has dual-screen menus, a bottom-screen
  map and items, touch/C-Stick camera, save previews, a CIA updater with stable and
  experimental channels, and diagnostic captures (L+R+A/X/Y). The owner's reading
  (2026-10-12): almost everything runs on the CPU, it is not known to reach 60 fps
  even on the New 3DS, there is no stereo 3D, and the Old 3DS does not run it.

## Status (2026-10-12)

Only what no other place records. Bugs: the open GitHub issues. Tasks: the unticked
boxes below. History: `git log` and the decisions log.

**Release line:** `release/v0.1.1` (on origin). Published: **`v0.1.0`** (2026-10-12, **beta**,
not on `main`): GitHub release with the CIA, QR, debug symbols, manifest and checksums, built by
`build-release.yml` on its first run. `main` is still upstream's v1.0: nothing stable published. `main` is upstream's v1.0.

**Branches:**

| Branch | What | State |
|---|---|---|
| `feat/native-menus` | U1: main, pause, skill, OPTIONS in tabs, save/load and yes/no dialogs drawn natively on the touch screen with the NES font; top screen frozen while a native menu is open | On the New 3DS 2026-10-13: pause menu 35 fps (tic-bound, ~5.5 ms of work; 9.4 before); owner tried tabs and touch, asked for half-cell sliders and native save/load (in the last build, being tried) |
| _(earlier)_ | Merged into `release/v0.1.1` 2026-10-13 after the owner's checks on the New 3DS: `feat/console-link` (P1.4/P1.5; not yet tried: `touch`, `console map`, the updater with the server on), `feat/perf-recorder` (P1.3), `fix/save-settings-on-quit` | |

**Priority** (2026-10-13): P1 debug tools and console link → U1 menus → U2 2D and
bottom screen on the GPU → P2 GPU world renderer and 60 fps on New 3DS → P3 stereo 3D
and P4 Old 3DS (order between these two to be set once P2.2 decides the renderer). P0.3
goes on alongside, with the P1.3 recorder.



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

- [ ] **U1** Menus that are quick to use and look good.
  *Spec:* options without submenus to enter just to flip a value (what is changed is
  visible and changed in place, by button and by touch); drawn directly for the bottom
  LCD at its own resolution with integer-scaled pixel fonts, instead of the engine's menu
  rendered on the top canvas, pixel-diffed and rescaled on the CPU; while a menu is open
  over a paused game the 3D view is not rendered again every frame.
  *Done when:* the owner approves the look on the console, and a pause-menu recording
  shows no 3D view or routing cost (9.4 fps at render scale 1.0 before).
- [ ] **U2** 2D and the bottom screen on the GPU.
  *Spec:* title picture, HUD, menus, FPS counter and the bottom-screen map/items drawn by
  the PICA200 (citro2d) instead of the SoftPoly raster and CPU loops; the FPS counter and
  other overlays at the LCD's native resolution whatever the render scale.
  *Done when:* recordings show the title's 70 ms and the bottom map's 34 ms gone, and the
  FPS counter is the same size in gameplay and menus.

- [ ] **U3** Language option: English and Spanish (castellano), the interface only.
  *Spec:* owner 2026-10-13: an option to pick the language, everything translated; for
  now only what the port draws (menus, options, dialogs, toasts, hints, the bottom
  screen, the updater), not the game's own text in the Legend of Doom pk3. Spanish
  letters the NES font lacks (Á É Í Ó Ú Ñ Ü ¿ ¡) drawn in its 8x8 style.
  *Done when:* the owner switches the language on the console and finds no English left
  in the interface in Spanish (and none Spanish in English).

## Phase 2: GPU world renderer, 60 fps on New 3DS

The base for everything else: 3D doubles the world drawing and the Old 3DS has a
third of the CPU, so neither is reachable with SoftPoly on the CPU.

- [ ] **P2.1** Where the frame goes today.
  *Spec:* from P0.3's recordings, split the New 3DS frame into game logic,
  BSP/visibility, SoftPoly raster (per core), presenter, audio, and idle; identify what
  stays on the CPU whatever the renderer (logic, BSP, ZScript).
  *Done when:* a note in `docs/` with those numbers and the CPU budget left for
  rendering at 60 fps (16.7 ms) and 30 fps.
- [ ] **P2.2** Choose the GPU path.
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
