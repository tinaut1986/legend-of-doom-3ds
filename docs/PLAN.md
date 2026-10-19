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

## Status (2026-10-18, evening)

Only what no other place records. Bugs: the open GitHub issues. Tasks: the unticked
boxes below. History: `git log` and the decisions log.

**Release line:** `release/v0.2.5` (renamed from `release/v0.2.4` right after its tag).
Published: **`v0.2.4`** stable (2026-10-15, built by the tag workflow), `v0.2.3` and `v0.2.2` betas,
**`v0.2.1`** stable, **`v0.2.0`** stable (the first), `v0.1.0` beta. `main` holds `v0.2.4`.

**In the line since v0.2.4, not yet published** (all merged 2026-10-17/18, each checked by the owner on
the console; the measurements are in the decisions log):
- 2DS memory: P4.1 bring-up, P4.5 census, P4.6 large-block arena, the first P4.7 / P4.8 cuts, P4.9 slim
  IWAD, the first P4.10 cut (`MAXWIDTH`), the music stored in the pk3; #17, #19, #20.
- Music on the Old 3DS (#16, closed): IMA ADPCM at 22 kHz, core 1 left at 30%.
- Interface: the clock / Wi-Fi / battery row, the loading screen's bars, the map-change loading box,
  the game's messages in its NES font (OPTIONS -> DISPLAY -> MESSAGES: BOTTOM / TOP), the 23 English
  pickup messages in Spanish, the GPU map-change fade (#18, still open), the overlay's GPU/CPU lines.
- Speed on the 2DS: the touch screen map every third tic and through a grid; settled static actors
  out of the two per-tic passes; the particle textures built once; the walk's sprite set, early
  sprite rejection and no visible planes under the GPU world. The owner's heavy MAP01 spot went from
  27-28 fps to ~44.7 (34 with forced 3D before the walk cuts).
- MAP01's black caves drawn black on the GPU (the sky showed through two spots of one cave).
- Debug: FORCE 3D, `lod3ds_sections`, `lod3ds_mapprofile`, `lod3ds_musicstats`, `lod3ds_audiocpu`,
  `lod3ds_maploadshot`, `lod3ds_visible`, `lod3ds_sectorsubs`, `lod3ds_tracesub`, `lod3ds_tracesolid`.

**Branches:**

| Branch | What | State |
|---|---|---|
| `fix/old3ds-mixer-22k` | #26: the Old 3DS / 2DS mix at 22050 Hz (fried sound, gaps) | Pushed, built (`367ee1989d7e`); the owner heard 22050 better live (cvar); **not merged**, the build itself not yet played |
| `fix/horizon-fog-fade` | the sky fades to MAP01's fog colour at the horizon (#14) | Parked, pushed, **not merged**: does not remove the pop of the tall mountains |

**Waiting for the owner's check:**
- The v0.2.4 stable via the updater (Release channel, from v0.2.1), still unchecked.
- The New 3DS has not run anything merged after `742ad9ba845f` (the music as ADPCM, the slim IWAD,
  the messages, the loading box, the speed cuts, the caves): play it there before the release.

**Priority** (2026-10-18, owner): pause the 2DS speed work here. Next the owner slips in another task
on its own topic branch (new session), merges it, then a beta / stable of the line, then back to what
is left to debug on the 2DS. Throughout: the 2DS fully playable, noticeable changes first, equivalent
effects that cost less are fine; #7 and #14 last.

**Start here:**
0. The owner's interim task: whatever they ask for, on a topic branch cut from `release/v0.2.5`.
   Done: #22 (control schemes), #24 (the black square in the sky over a cave) and #25 (the GC never
   ran on the 2DS after a long play: stutters, music gaps) merged 2026-10-18 after the owner's check
   on the 2DS; the New 3DS (CLASSIC by default) still to be played before the release. The 2DS's
   fried sound and gaps (#26) are on `fix/old3ds-mixer-22k`.
1. Release: write `docs/release-notes/<tag>.md` (everything above since v0.2.4, for the player), show
   it, merge it, then tag (beta or stable as the owner says) and rename the release branch at once
   (CLAUDE.md, "Releases" and "After tagging").
2. Back to the 2DS: the two save crashes, #21 (out of memory growing the save's JSON after a long
   play) and #23 (an object with no class in `WriteObjects`); then the heavy MAP01 spot's remaining
   frame: logic ~9 ms a tic, the walk's walls ~2.5 ms, the GPU capture's geometry ~3 ms
   (`lod3ds_sections` splits them). P4.2 (30 fps minimum) needs the P0.3 spots measured with 3D.
3. P4.10's rest and P4.11 (thinner compiled ZScript); the census leftovers in
   `docs/measurements/memory-census-2026-10-17.md`.
4. Later: #18 (the GPU fade like the old wipe), the New 3DS's 55 fps in stereo at some open-map spots,
   #14, #7.

**On the owner's consoles now (2026-10-18):**
- **New 3DS**, `192.168.1.133` on 2026-10-18 (it has been at `.141`): the owner installed
  `742ad9ba845f` (`0.2.5-dev.16.40+d609f5f`, the line as merged) with FBI from `/cias/`; FTP, the
  install-only marker `dev-ftp.txt`, saves `save0` "test" (MAP01, the spot Claude loads), `test2` and
  autosaves. DISTANCE HIGH.
- **2DS**, `192.168.1.139`: installed `92c16b35a0f6` (debug, the same code as `release/v0.2.5` at
  `82bc7d0`), by SITE INSTALL. The data folder has `allow-old3ds.txt` and `census-on-load.txt` (audio
  on), the census reports and the owner's saves (save0..2, auto0..3; first dungeon done). Claude's test
  saves were removed; Luma's `crash_dump_00000000` and `00000019` are from the Minish Cap port and
  were left. Installing over the running game near its memory limit can close it: then upload over
  ftpd.

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
  *Progress (2026-10-18):* with `test/2ds-play` the 2DS boots, plays MAP01 and the first dungeon to
  the end, now with audio (music still stutters, #16). It runs near the memory limit; P4.9-P4.11 are
  the way on.
- [ ] **P4.5** Memory census on the 2DS.
  *Spec:* a debug report (a `lod-link.py` command and a `startup.log` line) of what holds the heap at
  the title and after MAP01: engine objects by class (count, bytes), the compiled ZScript (classes,
  functions, strings, VM code), textures (software and GPU), cached lumps, sound and music, the
  TArray pool and the reserves; and the free list's shape (largest blocks, free chunks by size).
  *Done when:* a table in `docs/measurements/` for both points on the 2DS, and P4.6-P4.8's targets
  picked from it.
  *Result (2026-10-17, Claude):* `docs/measurements/memory-census-2026-10-17.md`; the tool is in
  `perf/memory-census` (`lod3ds_memcensus`, `census-on-load.txt`). Not checked by the owner yet.
- [ ] **P4.6** Keep small allocations away from the big blocks.
  *Spec:* after MAP01 loads, 8.3 MB is free on the 2DS but no block is larger than 255 KB: every
  multi-hundred-KB request (a PNG decode, renderer memory, a lump) fails. Either a size-class
  allocator for small requests (the bulk of the engine objects and the compiler's nodes), or one
  arena for large transient blocks with first-fit and coalescing (`m_alloc.cpp`'s TArray pool,
  generalised); P4.5's free-list shape decides. It should replace the 5 MB map-load reserve, the
  2 MB renderer reserve and perhaps the 10 MB pool. The census (P4.5) points to the arena for
  large transient blocks first: the free memory sits in 160-200 holes of 16 KB-1 MB that such
  blocks leave between long-lived ones; the small-block arena second (1.2 MB of chunk overhead,
  2 MB of unusable crumbs).
  *Done when:* after MAP01 loads on the 2DS the largest free block is 2 MB or more, and a new game
  plays the first screens.
  *Result (2026-10-17, Claude):* `perf/large-block-arena`. Blocks of 32 KB or more are carved down from
  the top of the heap region while newlib grows up (`large_arena_3ds.cpp`, `-Wl,--wrap=malloc`...);
  the pool and the map-load reserve are off. A new game plays MAP01 on the 2DS. The owner's longer play
  ran out of memory for real (2.6 MB free), so P4.7 / P4.8 decide how long it lasts. Not checked on the
  New 3DS.
- [ ] **P4.7** Lower the startup peak.
  *Spec:* about 131,000 blocks of 256 B or less (8.9 MB) are live at the title, from startup:
  find them by allocation site and cut them (census, P4.5). The ZScript compile adds 11.6 MB at its peak and gives back 4 MB;
  `after filesystem init` -> `cpu-detection-enter` adds 7.5 MB (not identified yet); the reserves
  take 7 MB at boot. Free the compiler's trees and tables once the code is built, find and cut the
  7.5 MB, and drop the reserves P4.6 makes unnecessary.
  *Done when:* the heap in use at the title on the 2DS is 5 MB or more below 37.9 MB
  (`softpoly-canvas-ready`, 2026-10-17).
- [ ] **P4.8** Lower MAP01's resident memory.
  *Spec:* `gc=` (17 MB after MAP01) is everything allocated through `M_Malloc`, not the actors:
  all engine objects together are 0.8 MB (census, P4.5). The level's arrays fill the 10 MB TArray
  pool (9.5 MB in MAP01). Candidates: smaller line / side / sector structures (fields this game
  never uses), what else lands in the pool, shared or 8-bit textures.
  *Done when:* MAP01 loaded on the 2DS leaves 15 MB or more of free heap (8.9 MB on 2026-10-17).
- [ ] **P4.9** A slim IWAD.
  *Spec:* the game loads all of `freedoom2.wad`. The engine makes an object for every texture,
  sprite and sound it defines (thousands, with names and a 0.5 MB lump directory), and the 2DS spends
  32 s in "Loading textures". Count what Legend of Doom really uses: textures, flats, sprites, sounds,
  the lumps the engine needs to start. Then build a minimal WAD from Freedoom at build time
  (`platform/3ds/build.sh`; never committed, like the other game data) and load it instead.
  *Done when:* the census shows the saving at the title and in MAP01 on the 2DS, the load time is
  measured before and after, and the first dungeon plays the same on both consoles.
  *Result (2026-10-18, Claude):* `perf/slim-iwad`, `platform/3ds/tools/slim-iwad.py` at build time:
  3610 -> 707 lumps, 28.7 -> 4.9 MB; the CIA 53.7 -> 29.6 MB (the FTP install 5:48 -> 3:02 on the 2DS).
  On the 2DS, same code with the full and the slim WAD: boot to the title 82 s -> 49 s ("texture
  manager" 43.6 s -> 11.7 s); title census 24.1 -> 21.8 MB in use (heap 12.62 -> 11.46, large arena
  11.51 -> 10.33), free gap 16.4 -> 19.2 MB. MAP01 from save0 and MAP02 look right; the only warning
  was Doom's armour icon (`ARM1A0`, gameinfo), kept since. Not played through by the owner yet.
- [ ] **P4.10** No CPU world-drawing buffers under the GPU world.
  *Spec:* with the GPU drawing the world, the software renderer only needs its BSP walk (what is
  visible). It still allocates canvases, SoftPoly's thread data, frame memory and wall/plane drawing
  buffers: 4-6 MB in the census. Allocate them only when the CPU draws the world. That happens when the
  GPU world is off: a debug cvar, or a fallback. Keep the fallback working, or decide in the decisions
  log to drop it on the Old 3DS.
  *Done when:* those buffers are gone from the MAP01 census on the 2DS, and the fallback still works
  where it is kept.
  *Progress (2026-10-18, Claude):* with SoftPoly in true colour the software renderer draws straight
  into the frame's canvas: there is no separate scene canvas, and the 1.8 MB of canvases in the census
  are SoftPoly's 2D textures (HUD, fonts), not world buffers. The world-side cut found so far is
  `perf/maxwidth-3ds`: `MAXWIDTH` 12000 -> 1024 and `MAXHEIGHT` 5000 -> 1024 on the 3DS, which sized
  every per-column array (445 KB in SoftPoly's thread data alone): -0.72 MB in the arena at the title
  and in MAP01, plus 88 KB of BSS; the save picture (drawn by the CPU) still comes out right. Left:
  the swrenderer's `RenderMemory` / drawers, created per thread whether the GPU draws or not.
  *Later (2026-10-18):* the walk itself got cheaper under the GPU world: no visible planes are made
  (-0.35 ms a frame at the heavy spot), the seen sprites go into a reusable set, and far or behind
  sprites are rejected before their frame is found (sprites 2.5 -> 1.2 ms).
- [ ] **P4.11** Thinner compiled ZScript.
  *Spec:* about 130,000 small blocks (~9 MB) are live at the title, mostly compiled ZScript: classes,
  functions, symbols, strings. Many are for things this game never uses (GZDoom's option menus: 354
  option items; Doom classes the mod does not touch). Trim `gzdoom.pk3` further (the 2026-10-15 trim
  left Doom's base) and free the compiler's structures after the build (the +11.6 MB compile peak).
  *Done when:* the title census on the 2DS has 5 MB or more less in the small heap than on 2026-10-18,
  and nothing the game uses is missing (the compiler's messages in `fatal.log`).
- [ ] **P4.2** 30 fps minimum on the Old 3DS.
  *Done when:* the P0.3 spots never drop under 30 fps (1% low) on the 2DS, steady pacing.
  *Progress (2026-10-18):* the owner's heavy MAP01 spot (around 1251, 3609) went from 27-28 fps to
  ~44.7 on the 2DS in one day of cuts (decisions log); with FORCE 3D it was 34.3 before the walk
  cuts. The P0.3 spots are not measured yet.
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
  save and load menus put their picture and text on a transparent layer of their own, shown by
  the GPU over it (#10, #11: the picture's cache is keyed by the slot, not the texture pointer).
  The save picture is rendered with the GPU world suspended.
- **2026-10-15** Distant walls pop (#14): the tall mountains at the horizon are dropped at the
  cull distance (1536 + DISTANCE x 512) and a whole pale block appears between two frames, in
  front of the clouds; fading the sky to the fog colour (`fix/horizon-fog-fade`) does not hide
  it. The fix is to keep drawing the far tall segments, or fade them to transparent over a margin.
  Not done: MAP01's TEXTMAP has every sector at height 0 (ACS / the level postprocessor builds
  the relief at load), so "tall" would have to be read from the live planes each frame. Owner:
  "if it is a lot of work, it is not that serious".
- **2026-10-15** Owner's decisions: no dynamic lights from fire for now (#13 closed); the clock
  muting every sound is wanted fixed (#15); DISTANCE stays HIGH on the New 3DS.
- **2026-10-15** Production build checked on the New 3DS by the owner (`--mode prod`, `debug_tools=0`):
  runs, no DEBUG tab or overlay, FRAMES works, the messages and the progress bar are right. A
  production build's FTP is install-only: the saves and logs cannot be read until a debug build
  is installed again (`--install` works over it).
- **2026-10-15** #15 fixed in `fix/clock-keeps-sfx`: `PowerTimeFreezer` pauses the music only
  (`S_PauseSound(false, true)`) and resumes it with `S_ResumeSound(true)` when the effect ends; the
  effects were paused with it, so the sword was mute. Checked by the owner on the New 3DS.
- **2026-10-15** `feat/gpu-3d-floors` merged into the release line after the owner walked MAP01's cave mouths
  (entering one teleports to its interior, by design). P2.3 against the game's own data (the ten maps' UDMF and the ACS): only MAP01 has 3D
  floors (7 `Sector_Set3dFloor` lines, ~40 sectors of 128x128: additive cave mouths `CAVEENT`/`CAVEENTB`
  and solid `OBRKB` blocks). No real portals (`portal_ceil_norender` is a flag that draws nothing), no
  translucent lines, no coloured light or fade, no scrolling, no decals (monsters carry
  `NOBLOODDECALS`), animated textures only the water flats. So `feat/gpu-3d-floors` (planes and sides of
  every ffloor, opaque / translucent in depth order / additive batched per texture) completes what the
  game needs: A/B captures (GPU against `lod3ds_gpu_world 0`) at 8 spots are identical, the water
  oscillates on the GPU, the flashes ride the GPU 2D. Not looked at: the waterfall `WTRFL01` (not found
  in MAP01's sidedefs) and views from inside a 3D floor. The ffloor sides' light is the target
  sector's, not the control sector's.
- **2026-10-15** #12 measured at `test2` (save slot 1) on the New 3DS, overlay closed, fresh boots: the
  GPU is the limit, not the CPU (12 draws, 3.4k vertices; GPU 13.5 ms mono, 17.7 stereo, with
  `capture` 2.5 ms and `walk` 3.7 ms), so the world's RGBA8 textures read from linear RAM were the
  cost. World textures (walls, flats, sky) are now built as 565 when opaque, 5551 when their alpha is
  binary, 8888 otherwise (`BuildTexture`, cvar `lod3ds_gpu_texture16`, debug builds): stereo
  48.2 -> 61.4 fps, mono 76.7 -> 86.0, and half the memory of those textures (good for P4). Textures are
  kept across map loads, so the cvar only shows on a fresh boot. Sprites and 2D stay 8888.
- **2026-10-17** P4.1 on the 2DS (owner at the console, Claude reading logs over FTP and, at the
  end, installing with `--install` and starting the new game itself with `lod-link.py`). Found, in
  order:
  - The audio mixer thread crashes at startup (#16). `no-audio.txt` (debug builds) skips the audio
    init.
  - Then a new game failed on a 2 MB request. The "largest free block" probe and an in-handler
    retry said there was room. **Both were wrong**: GCC drops a `malloc` immediately followed by
    `free` and folds the test to "succeeded". Honest figures need a compiler barrier on the
    pointer (`memory.cpp`, `d_main.cpp`).
  - With the barrier the picture is fragmentation: 10.5 MB free, the largest block 1.7 MB. Four
    fixes, in `fix/2ds-memory`, each moved the failure one step further:
    - `operator new` hands back the 5 MB map-load reserve when a block finds no room.
    - MAP01's TEXTMAP is read from the map WAD instead of cached whole (a `FResourceLump` reader
      locks the whole lump).
    - The Old 3DS skips the map backup copies (`loadsides` / `loadlines` / `loadsectors`). Its
      saves store those fields whole, so **a save written on the New 3DS will not load correctly
      on the 2DS** with this.
    - `RenderMemory` blocks are 256 KB on the 3DS instead of 1 MB.
  - MAP01 now loads (8.9 MB free). The first frame fails on a 512 KB PNG decode with the largest
    free block at 255 KB.
  - Each further one-off fix would only meet the next large request. The owner chose to plan the
    memory work properly (P4.5-P4.8) before going on.
  - A `std::bad_alloc` that bypasses the new handler (`FStringData::Alloc`,
    `RenderMemory::MemoryBlock` throw it directly) is located by the `__cxa_throw` wrap, which logs
    the code addresses on the stack.
- **2026-10-17** P4.5's census on the 2DS (`docs/measurements/memory-census-2026-10-17.md`).
  Findings:
  - `gc=` counts everything allocated through `M_Malloc`, not the actors: the engine objects are
    0.8 MB in MAP01. Light decorations would save little.
  - About 150,000 small blocks are live, most of them from startup.
  - The 8.7 MB free in MAP01 lies in holes of 16 KB-1 MB plus 2 MB of crumbs.
  - So P4.6 starts with an arena for large transient blocks. P4.8 turns to the level's arrays
    (the 10 MB pool) instead of the actors.
  - Side finding: `version.sh` gives `0.0.0-dev` on a topic branch once its release branch has
    moved on, and the console's updater then offers the published release.
- **2026-10-17** P4.6: a large-block arena carved down from the top of the heap region (newlib keeps
  the bottom) took the 2DS from "fails at MAP01's first frame" to "plays MAP01". Census by caller
  (`docs/measurements/memory-census-2026-10-17.md`): the arena holds 27 MB in MAP01. The biggest
  cuttable items are the true-colour software textures (2.75 MB), the string table with every language
  (2.15 MB), the Unicode hex font (2 MB), the canvases (1.8 MB) and the debug network buffer (1 MB).
  Two lessons:
  - GCC drops a plain store to a global just before `malloc`, since it assumes malloc reads no
    program memory, so the caller hint has to be `volatile`.
  - `version.sh` needs `release/v0.2.5` merged into stacked branches (`0.0.0` otherwise).
- **2026-10-17** First P4.7 / P4.8 cuts (`perf/memory-cuts`): only English / Spanish string tables, a
  Latin-only hex font, no software precache under the GPU world, a 256 KB socket buffer and one raster
  worker on the Old 3DS. MAP01 on the 2DS: 44.6 -> 31.9 MB in use, 20.6 MB free. Another system
  language now falls back to the default (English) strings on the 3DS.
- **2026-10-18** The 2DS plays through the first dungeon (`test/2ds-play`). Findings on the way:
  - **Map backups.** Skipping them on the Old 3DS was not safe: `SerializeArgs` and the colormap
    serializer compare against the default directly, and hub snapshots crashed. They are made again
    (~3.8 MB).
  - **GPU textures.** Their copies were never freed, map after map; linear memory reached 140 KB in
    the dungeon and frames fell back to the CPU (#19).
  - **R held.** `PlayerPawn.FireWeapon` sends a noise alert every tic, and the flood through MAP01's
    open sectors cost ~18 ms (#20).
  - **Small heap.** After long play it ends in crumbs: small requests now fall back to the large
    arena.
  - **Audio.** With the memory freed it no longer crashes the 2DS, but the music stutters (#16,
    core 1 at 30%).
  - **Next step.** The owner wants noticeable engine changes and accepts equivalent effects that
    cost less, hence P4.9 (slim IWAD), P4.10 (CPU drawing buffers) and P4.11 (compiled ZScript).
- **2026-10-18** Merged the whole 2DS stack after the owner's New 3DS check: fps hold (lowest seen
  55 in stereo at some open-map spots), music, dungeons, R held. The GPU fade (#18) is not quite the old
  wipe; the owner wants it matched later, on the GPU.
- **2026-10-18** Music on the Old 3DS (#16). Raising core 1's limit needed the RSF's `MaxCpu` (0x9E
  capped it at 30%; now 0xD0), and 80% stopped the gaps but starved the system: slower game, ftpd
  crawling, the 2DS froze on closing. minimp3 needed 129% of its time even at 22 kHz
  (`lod3ds_musicstats`), so the music became IMA ADPCM (a ZMusic decoder, bit-exact with ffmpeg) at
  22 kHz mono: no underruns at 30%, CIA +2.3 MB, no RAM.
- **2026-10-18** Interface choices of the owner: a map-change loading box (dimmed last picture, the
  area's name, the phase, a bar timed on the 2DS and scaled by the map's size); the game's messages
  in its own small NES font on a black band, the place an option (TOP / BOTTOM of the top screen,
  BOTTOM by default; they meant the top screen's bottom, not the touch screen).
- **2026-10-18** 2DS speed at the owner's 27 fps MAP01 spot, measured with the perf recorder and
  `lod3ds_sections`. What paid off: the touch screen map every third tic and through a 512-unit grid
  (6.2 -> 0.8 ms a frame), settled static actors out of ClearInterpolation and the light/effect pass
  (-0.9 ms a tic), the particle textures built once instead of every frame (2.15 ms a frame, the
  biggest single find), the walk's std::set of seen actors replaced and far / behind sprites rejected
  early (2.5 -> 1.2 ms), no visible planes under the GPU world (1.0 -> 0.7 ms). Render scale did not
  matter: the walk costs per subsector, not per column. The New 3DS builds the map every third tic
  too, so both play alike (owner).
- **2026-10-18** Not to do on the 2DS: `lod3ds_gpu_world 0` in MAP01 (the CPU path's software textures
  ran it out of memory in seconds), and console saves after a long play (#21, #23). To reach a spot
  again, `warp X Y` and steer the camera with `lod-link.py stick CSTICK`.
- **2026-10-18** MAP01's caves are a BLACK floor under a sky ceiling, with a one-unit solid 3D floor at
  the cliff top; their sky is the SkyViewpoint the port does not draw, so the swrenderer leaves them
  black. The GPU world now draws their ceiling, the gap above the mouth and the walls above the cliff
  top in black, and a visible cave whole (the 3D floor's sides made the walk hide its far half).
- **2026-10-18** #22, control schemes: each scheme stores one action per remappable button as a list
  of tokens (`lod3ds_buttons_classic` / `_camera`, empty = its defaults, so a later change of a default
  reaches players who never edited it), applied over the engine's bindings after `DoKeySetup`; the
  BUTTONS screen edits the active one through non-archived `lod3ds_button_*` cvars, so it is an
  ordinary option menu the native drawer already handles. `lod3ds_control_scheme` starts at -1 and is
  fixed by the model on the first start. Walking into a use line activates it only when its script is
  one of the six of the issue (by name, not every ACS use line: shops and old men stay on interact).
  The interaction hint shows once per run of the game until the player interacts with the button once
  (`lod3ds_use_hint`), not per save: a flag in the save would need the game's own state. It leaves the
  walk-into lines out (some need a key or the bracelet first): the maps have no other front-use line,
  so today the hint never shows; it stays for anything the game adds.
- **2026-10-18** #25: GZDoom's `GC::AllocBytes` counts every `M_Malloc` block, so on the 2DS it is ~24 MB
  after a map load and the stock next-cycle threshold (150%) was beyond the heap: the collector never ran
  again and the `Sheeld` the shield spawns every Ready tic piled up (7391, 8.6 MB). The 3DS caps the
  threshold at the estimate + 1 MB. Diagnosed live with `lod3ds_memcensus` and by peeking `GC::Threshold`
  / `Estimate` (`nm` on the ELF; `lod-link.py peek` takes the address); `gc full` freed 10 MB at once.
- **2026-10-19** #26: on the Old 3DS the NDSP mixer and the music decoder share core 1 (30% for the app);
  at 32000 Hz the mixer fell behind when many sounds played. 22050 Hz (the music's rate) cut the
  decoder's share of the core from 56% to 13%, and the owner heard it better; the New 3DS keeps
  `snd_samplerate`. Left as is: the music's fixed 3x gain (picks at -3 to -7 dB with it), a candidate
  if some clipping remains.
