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
| `fix/save-settings-on-quit` | Settings were lost on QUIT GAME (upstream skips the final config save) and HOME -> Close: the config is now written when the player leaves the option menus, and before QUIT GAME | Checked on the New 3DS 2026-10-13 (FTP SERVER kept after closing) |

**Priority** (2026-10-12): P0.1 → P0.2 → P1 debug tools and console link → P0.3
baseline → P2 GPU world renderer and 60 fps on New 3DS → P3 stereo 3D and P4 Old 3DS
(order between these two to be set once P2.2 decides the renderer).



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
- [ ] **P1.3** Perf recorder (CSV per frame) + `tools/perf-csv/analyze.py`.
- [ ] **P1.4** FTP server in the game; install a CIA over FTP and relaunch.
- [ ] **P1.5** Remote link (keys, touch, screenshot, state, memory by symbol) for
  Claude; `CLAUDE.md` "The console in the loop" written when it lands.

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

_Empty until P0.3._

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
