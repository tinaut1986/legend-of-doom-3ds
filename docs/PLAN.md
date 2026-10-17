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

## Status (2026-10-17)

Only what no other place records. Bugs: the open GitHub issues. Tasks: the unticked
boxes below. History: `git log` and the decisions log.

**Release line:** `release/v0.2.5` (renamed from `release/v0.2.4` right after its tag).
Published: **`v0.2.4`** stable (2026-10-15, built by the tag workflow), `v0.2.3` and `v0.2.2` betas,
**`v0.2.1`** stable, **`v0.2.0`** stable (the first), `v0.1.0` beta. `main` holds `v0.2.4`.
Merged into the line since v0.2.4: the clock / Wi-Fi / battery in the bottom title row, and the
loading screen's two progress bars and message (owner saw them on the 2DS, 2026-10-17).

**Branches:**

| Branch | What | State |
|---|---|---|
| `fix/horizon-fog-fade` | the sky fades to MAP01's fog colour at the horizon (#14) | Parked, pushed, **not merged**: it does not remove the pop of the tall mountains (see #14); harmless, unchecked as a smoother horizon |
| `perf/large-block-arena` | P4.6: the large-block arena; the census by caller | Pushed, **not merged**; on top of `perf/memory-census` (needs it); unchecked on the New 3DS |
| `perf/memory-census` | P4.5: the memory census (`lod3ds_memcensus`, `census-on-load.txt`) | Pushed, **not merged**; cut from `fix/2ds-memory` (needs it) with `release/v0.2.5` merged in for the version |
| `fix/2ds-memory` | P4.1 bring-up: `no-audio.txt` (#16), honest allocation-failure diagnostics, and the map-load memory fixes that get MAP01 loaded on the 2DS (decisions log, 2026-10-17) | Pushed, **not merged**: works on the 2DS up to the first frame; changes the Old 3DS save format (full sides/lines/sectors); unchecked on the New 3DS. The base for P4.5-P4.8 |

**Waiting for the owner's check** (done by Claude only, or not seen yet):
- The v0.2.4 stable: that the console's updater (Release channel) offers it from v0.2.1 and shows its
  6 notes (the production build itself was checked by the owner on the New 3DS).
- `fix/2ds-memory` on the New 3DS: that nothing regressed (MAP01, a save and its load).

**Priority** (2026-10-17, owner): the 2DS's memory, planned as P4.5-P4.8, "plan it well and then
start"; then #16 (audio on the Old 3DS); #7 and #14 last. The owner asked again whether the engine
can be restructured to need less RAM: yes, that is P4.7 / P4.8.

**Start here:**
0. **P4.7 / P4.8**, from `perf/large-block-arena`: the cuts listed in
   `docs/measurements/memory-census-2026-10-17.md` ("With the large-block arena"), biggest and cheapest
   first: the string table's other languages, the hex font, software textures, the debug network buffer,
   SoftPoly's second thread data. Then check the whole stack on the New 3DS.
   The 2DS needs `allow-old3ds.txt` and `no-audio.txt` in its data folder (both there now,
   with `census-on-load.txt`). P4.7 and P4.8 make the room last.
1. **#16** after P4.6, so the audio can be tested in a level.
2. Nothing is half done or uncommitted.

**On the owner's consoles now (2026-10-17):**
- **New 3DS**, `192.168.1.141` (the 2DS has used the same address): a debug build
  `0.2.4-dev.56.1+b4d6b62`: the v0.2.4 line plus the parked `fix/horizon-fog-fade`, FTP server on,
  the install-only marker `dev-ftp.txt`, saves `save0` ("test", MAP01, the spot Claude loads),
  `test2` (the 48-50 fps spot of #12) and three autosaves. DISTANCE is HIGH (the owner's setting);
  FRAMES was last left at NO SKIP by Claude.
- **2DS**, `192.168.1.139` (`.3ds_ftp_ip` still says `.141`): a debug build of `perf/memory-census`,
  `perf/large-block-arena`, `0.2.5-dev.10.8+c74ffc4`, build `9514e8e43726`, FTP server on; a new game
  plays MAP01 (no audio) until memory runs out. The data folder has `allow-old3ds.txt`, `no-audio.txt`, `census-on-load.txt` and the
  two census reports, no saves. A new game closes to HOME at MAP01's
  first frame. `/cias/` holds the CIAs uploaded over ftpd during the session. Luma's
  `crash_dump_00000000` and `00000019` are from the Minish Cap port and were left.

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
  *Progress (2026-10-17):* boots to the title and loads MAP01 on the 2DS (`fix/2ds-memory`, with
  `no-audio.txt`, #16); a new game then fails at the first frame on a fragmented heap. P4.5-P4.8
  are the way on.
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
