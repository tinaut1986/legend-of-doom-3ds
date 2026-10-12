# Project notes for Claude

Native Nintendo 3DS port of **Legend of Doom** (DeTwelve Games' GZDoom conversion of
the first *The Legend of Zelda*). This is the owner's fork
(`tinaut1986/legend-of-doom-3ds`) of EstebanPdN's port, which runs GZDoom 4.7.1 with
Freedoom: Phase 2 on the New 3DS. The owner already shipped three ports of the same
kind, Metroid: Zero Mission (`../mzm`), Super Metroid (`../sm-3ds`) and The Minish
Cap (`../zelda-tmc-3ds`); they are the reference for workflow, debug tools,
bottom-screen UI and performance work. `../zelda-tmc-3ds` is the closest one: also
a fork of an EstebanPdN port, set up the same way.

**Start every session by reading [`docs/PLAN.md`](docs/PLAN.md).** It holds the
phase roadmap, the task specs with acceptance criteria, and the decisions log.
The next session starts from that file, not from chat history.

Each fact lives in one place; keep that place current in the same change that alters it:

| What changed | Update |
|---|---|
| A task finished | tick it in PLAN; anything non-obvious goes to the decisions log |
| A task added, changed, split or dropped | its spec in PLAN (IDs are never reused or renumbered); a dropped one stays, struck through, with the reason in the decisions log |
| Priorities (the owner reorders, or a task becomes blocked) | the **Priority** line in PLAN's Status, and why in the decisions log |
| A branch opened, handed to the owner for testing, confirmed or merged | the branch table in PLAN's Status |
| A bug found or fixed | its GitHub issue (what, which commit, what to check); never a copy in PLAN |
| A tool, option, file format or workflow added or changed | its doc (`docs/debug-tools.md` once it exists, `platform/3ds/README.md`, `platform/3ds/UPDATER.md`, this file's Build section) |

PLAN's Status section holds only what no other place records: open branches, what
waits for the owner's check, and the priority order. Do not grow it into a summary of
issues or specs.

## Language

- Chat with the owner in **Spanish (castellano)**.
- All code, comments, identifiers, commit messages and docs: **English**.
- Exception: the inherited code and docs keep their own style. That includes the
  GZDoom tree, EstebanPdN's 3DS code and his Spanish notes (`ROADMAP.md`,
  `platform/3ds/*.es.md`, `docs/history/`). Do not churn or retranslate them;
  convert what you touch for another reason.

## Where things come from

| Path | Origin | Notes |
|---|---|---|
| `src/`, `libraries/`, `wadsrc*/`, `tools/`, `specs/`, `fm_banks/`, `soundfont/` | GZDoom 4.7.1 (`LOD3DS_GZDOOM_BASE_REV` in `platform/3ds/dependencies.sh`) | The engine. Edited in place where the port needs it, guarded for 3DS. |
| `src/common/platform/3ds/` | EstebanPdN, now ours | The 3DS platform layer: input, lifecycle, diagnostics (L+R+A/X/Y), bottom screen, the SoftPoly → PICA200 presenter, the CIA updater (`update/`), and the console link (`ftp_server_3ds`, `debug_link_3ds`, `cia_install_3ds`, `net_3ds`, from `../mzm`). |
| `platform/3ds/` | EstebanPdN, now ours | Build (`build.sh` and its profiles), pinned dependencies (`dependencies.sh`), patches for the fetched libraries (`patches/`), CIA metadata (`cia/`), assets, romfs, host tests (`tests/`) and scripts (`tools/`). |
| Fetched at build time | SDL2, ZMusic, minimp3, OpenAL Soft/NDSP, NovaGL, Legend of Doom (`emawind84/legend-of-doom`), Freedoom | Pinned by revision/checksum in `dependencies.sh`. Change them through a patch in `platform/3ds/patches/`, never in the fetched checkout. |

Other local checkouts: `../zelda-tmc-3ds`, `../sm-3ds` and `../mzm`, the reference ports.

## Git and GitHub

- **Every push, PR, issue and release goes to `tinaut1986/legend-of-doom-3ds`.** Never
  to EstebanPdN, emawind84 or the GZDoom projects.
  - `origin` = `tinaut1986/legend-of-doom-3ds`, `upstream` = `EstebanPdN/legend-of-doom-3ds`.
  - The push URL of `upstream` is `DISABLED` as a safety net (local config; redo it
    in a fresh clone with `git remote set-url --push upstream DISABLED`).
- `gh repo set-default tinaut1986/legend-of-doom-3ds` has been run
  (`remote.origin.gh-resolved=base`). Re-run it in a fresh clone, otherwise `gh`
  targets the fork parent.
- Issues on `tinaut1986/legend-of-doom-3ds` are used for playtest bugs (they have to
  be enabled in the repository settings first; see PLAN P0.2).
- Taking upstream changes: `git fetch upstream`, merge `upstream/main` into a topic
  branch cut from the active release branch, resolve, and bump
  `platform/3ds/upstream_version.txt` to the upstream version merged. Upstream still
  writes to `sdmc:/3ds/legend-of-doom/` and checks title ID `0x5A2D0`: keep our
  `app_paths_3ds.h` values in whatever it adds. Never
  rebase our history onto theirs.

### Branches

- `main` = stable. Only receives `--no-ff` merges of a finished release branch.
- `release/vX.Y.Z` = the current release line; work accumulates here, and being
  on it means "not stable yet". Active line: see PLAN's Status.
- Topic branches (`feat/...`, `fix/...`, `perf/...`, `chore/...`, `docs/...`) are
  cut from the active release branch and merged back into it with `--no-ff` once
  the owner has checked them on the console. **One topic per branch, always cut from
  the release branch, never from another topic branch**: stacking them makes none
  mergeable on its own. The one exception is a task that truly needs another
  unmerged branch; say so in PLAN's branch table.
- Never commit directly on `main` or `release/*`. Commit only when the owner asks.
- **`main` has no `CLAUDE.md` or `docs/PLAN.md` until the first release merge**: it is
  still upstream's v1.0 and GitHub's default branch, so a fresh clone lands on it
  without these instructions. Check out the active release branch first
  (`git checkout release/vX.Y.Z`; PLAN's Status names it) and cut topic branches from
  there.

### Versions

Our own numbering, independent of upstream's: `vX.Y.Z`, starting at `v0.1.0`.
Minor = milestone, patch = fix round. Ask before a minor bump; never infer one.

- Upstream's tags have two components (`v0.31`, `v1.0`, `v1.1`) or an experiment
  suffix (`v0.8-E3`). Those tags are in this repository too. **Only three-component
  tags are ours**; never create another kind.
- The version is derived from git by `platform/3ds/tools/version.sh`: an exact
  `vX.Y.Z` tag gives `X.Y.Z`; anything on (or cut from) `release/vX.Y.Z` gives
  `X.Y.Z-dev.<commits since main>[.<commits since the release branch>]+<hash>`.
  Nothing to bump by hand. `LOD3DS_VERSION` overrides it (the release workflow does).
- The upstream version this fork is based on is `platform/3ds/upstream_version.txt`,
  shown next to ours (developer overlay, boot log, build manifest, dumps).
- This fork installs as **its own application**, named "Legend of Doom 3DS*" on the
  HOME menu (the `*` tells it from upstream's), title ID `0x5A2D1`, product code
  `CTR-P-LDOT` (`platform/3ds/cia/legend-of-doom-3ds.rsf`; upstream is `0x5A2D0`), data
  folder `sdmc:/3ds/Legend of Doom 3DS/`. Both live in
  `src/common/platform/3ds/app_paths_3ds.h` (`LOD3DS_APP_DIR`, `LOD3DS_TITLE_ID`, which
  the updater checks); never hard-code the folder again. It never touches the upstream
  app's `sdmc:/3ds/legend-of-doom/`, so both can be installed side by side. Only the
  NovaGL diagnostic patches (`platform/3ds/patches/novagl-hardware-*.patch`, unused by
  the shipped profiles) still name the upstream folder.

### Releases (installer on GitHub)

`.github/workflows/build-release.yml` builds the `hardware-hybrid` CIA on any `vX.Y.Z`
tag push (and on manual dispatch) and publishes a GitHub Release with it
(`legend-of-doom-3ds-vX.Y.Z.cia`, the only name the updater accepts), a QR code, the
debug symbols, the build manifest and checksums. No 3DSX or SD zip: the updater
installs CIAs only (the build still makes them locally). The channel depends on whether
`main` can reach the tagged commit:

| Built from | Channel |
|---|---|
| Tag on an unmerged `release/*` branch | `Beta` (pre-release) |
| Tag on `main`, or on a commit merged into `main` | `Release` |
| Manual dispatch on any branch | `Beta` |

`.github/workflows/build-3ds.yml` (upstream's) only builds the `hardware-safe` profile
and checks the packages, for pull requests and pushes to `main` and `release/**`; it
publishes nothing.

**First, for either path:** write `docs/release-notes/<tag>.md` on a topic branch, show it
to the owner and merge it into the release branch **before** the tag exists. Do it unprompted
whenever a tag is about to be made.

**Beta**: tag the release branch, push branch then tag.

```sh
git tag -a v0.1.0 -m "v0.1.0"
git push origin release/v0.1.0
git push origin v0.1.0
```

**Stable**: merge into `main`, tag the merge, push **main before the tag**
(the tag build checks reachability from `origin/main`).

```sh
git checkout main
git merge --no-ff release/v0.1.0
git tag -a v0.1.0 -m "v0.1.0"
git push origin main
git push origin v0.1.0
```

A tag already shipped as a beta is promoted by re-running the workflow from the
Actions tab **on the tag itself** (not on `main`). That is only valid if **nothing was
committed after the beta's tag**; otherwise tag the next version.

### Release notes (shown in the updater)

One file per tag, `docs/release-notes/vX.Y.Z.md` (rules in its README): 3 to 6 short
lines, each starting with `- `, plain text for the player, in English. The CI copies it
into the release body between `<!-- lod-notes -->` markers; the console's updater
(`src/common/platform/3ds/update/`, a 42-column view) shows that block. The marker
must stay identical in the workflow and the updater.

A **stable** release lists everything since the previous *stable* tag, betas included; a
**beta** lists only what is new since the previous tag.

### The channel is baked into the binary

The workflow passes `LOD3DS_CHANNEL=beta|release` to `platform/3ds/build.sh` (local
builds are `dev`). The generated `lod3ds_version.h` then carries `LOD3DS_PORT_IS_BETA`
and `LOD3DS_PORT_VERSION_LABEL` ("X.Y.Z BETA"); `LOD3DS_PORT_VERSION` stays the plain
number (user-agent, comparisons). The updater treats an installed beta as older than
the stable release of its own number, and never offers a beta its own pre-release page.
Betas and dev builds carry `LOD3DS_DEBUG_TOOLS=1`, stable releases `0`.

### After tagging

After **every** tag, beta or stable, rename the release branch to the next patch at once
(before more work or any build) and delete the old remote branch; otherwise dev builds call
themselves `X.Y.Z-dev.N` and the console offers the already published `X.Y.Z` as an update.
Do the steps one at a time and check with `git ls-remote`.

```sh
git branch -m release/v0.1.0 release/v0.1.1
git branch --unset-upstream
git push -u origin release/v0.1.1
git push origin --delete release/v0.1.0
```

## Game data and legal

- Never commit, package or upload a commercial Doom IWAD, a NES ROM, a save, or any
  build or dump that contains them. The game data the port ships (Freedoom: Phase 2,
  `LegendOfDoom.pk3`, the engine's pk3s) is fetched or built, never committed;
  `build-3ds.yml` fails if a `.wad`, `.pk3`, `.nes`, `.sav`, `.cia`, `.3dsx`... is
  tracked.
- The console needs its own DSP firmware at `sdmc:/3ds/dspfirm.cdc` for audio; it is
  never distributed.
- License: GPL-3.0 (see `LICENSE`, `CREDITS.md`, `THIRD-PARTY-LICENSES.md`, `docs/licenses/`).
  Keep the notices; the build copies them into the packages.

## Build

Toolchain in `/opt/devkitpro` (devkitARM, libctru, 3DS CMake). The build fetches and
patches its dependencies into `build-3ds/` (see `platform/3ds/README.md`); `makerom`
and `bannertool` are taken from `PATH`/`LOD3DS_TOOLS_ROOT` or downloaded pinned.

```sh
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
./build_3ds.sh                                  # interactive menu (hardware-hybrid, debug)
./build_3ds.sh --no-ftp                         # build only
./build_3ds.sh --ftp 192.168.1.50               # build + upload to /cias/ on the console
./build_3ds.sh --install 192.168.1.50           # build + install over the running game + relaunch
./build_3ds.sh --mode prod --no-ftp             # no debug tools, as a stable release
./platform/3ds/build.sh hardware-hybrid         # the bare build, the profile releases ship
./platform/3ds/build.sh                         # hardware-safe, the CPU-only recovery profile (CI)
./platform/3ds/test-patches.sh && git diff --check          # what CI checks before building
python3 -m unittest discover -s platform/3ds/tests -p 'test_*.py'   # contract tests
python3 platform/3ds/tests/run-update-tests.py --jansson-prefix DIR # updater host tests (UPDATER.md)
```

Outputs go to `build-3ds/dist/`: `legend-of-doom-3ds-v<VERSION>-<profile>-<build id>`
`.3dsx`, `.cia`, `-sd.zip`, `-debug-symbols.zip`, plus `BUILD-MANIFEST.txt` and
`SHA256SUMS.txt`. The profiles are described in `platform/3ds/README.md`; `release` is a
legacy NovaGL experiment despite its name. The build ID is a hash of the source tree;
it, the version and the channel live in the generated
`build-3ds/game/generated/lod3ds_version.h`, so a new commit rebuilds only the few files
that include it. `./build_3ds.sh` (`platform/3ds/tools/build_3ds.py`, from
`../zelda-tmc-3ds`) can scan the LAN for the console's FTP server (port 5000) and upload
the CIA as `cias/legend-of-doom-3ds-<version>.cia`; the last IP is kept in `.3ds_ftp_ip`
(gitignored).

On the owner's Linux machine (GCC 15, no `libsdl2-dev`, no sudo) the host tools that
`build.sh` compiles first (ZMusic, GZDoom's `re2c`/`lemon`/`zipdir`) need two extras that
CI's container does not: `CXXFLAGS="-include cstdint"` (GCC 15 is stricter than the
pinned ZMusic) and `SDL2DIR=$HOME/.local/sdl2-host` (a static host SDL2 built from the
2.32.10 source; rebuild it there if missing). Only the first configure reads them;
delete `build-3ds/host-zmusic` / `build-3ds/host-gzdoom` to redo it.

```sh
SDL2DIR=$HOME/.local/sdl2-host CXXFLAGS="-include cstdint" ./platform/3ds/build.sh hardware-hybrid
```

The contract tests (`platform/3ds/tests/test_*.py`) assert strings in the source: when
one fails after a deliberate change, update the test in the same commit.

Diagnostics already in upstream (all builds): **L+R+A** quick capture, **L+R+X** full
capture with memory, **L+R+Y** delete captures, into `sdmc:/3ds/Legend of Doom 3DS/dumps/`
(`src/common/platform/3ds/diagnostics_3ds.cpp`); updater log in `update.log`. The
developer overlay (OPTIONS) shows our version, the upstream one and the build ID.

Test on hardware whenever performance is involved; Azahar is fine for logic but says
nothing about real timings. Upstream supports only the New 3DS family; this fork's
goal (PLAN) is New 3DS and Old 3DS/2DS with stereo 3D, so the owner has both a New 3DS
and a 2DS to test on.

## The console in the loop (for Claude)

A debug build (`LOD3DS_DEBUG_TOOLS=1`) running on the owner's console can be reached from
this machine (same LAN, the IP the build assistant saved in `.3ds_ftp_ip`) once
**OPTIONS -> DEVELOPER -> FTP SERVER** is on; it comes back by itself after a relaunch.
Nothing needs FBI or closing the game:

```sh
./build_3ds.sh --install <ip>                   # build, upload, install over the running game, relaunch
curl -u lod3ds:NXv7Of1lx5kmlidg "ftp://<ip>:5000/3ds/Legend%20of%20Doom%203DS/"   # list / fetch files
platform/3ds/tools/lod-link.py wait && platform/3ds/tools/lod-link.py run SCRIPT  # drive the game
```

**The FTP login** (debug builds and the install-only server below): user `lod3ds`, password
`NXv7Of1lx5kmlidg`, defined in `src/common/platform/3ds/ftp_credentials_3ds.h` (the tools read
it from there; keep both in step). The password is in the repository and the binary: it keeps
an accidental client out, it is not a secret.

**Stable builds** have no FTP unless `dev-ftp.txt` exists in `/3ds/Legend of Doom 3DS/` (the
owner creates it once, over FTP, while a debug build is installed): then they start an
install-only server (only `update/`, no remote control), enough for `--install`. Players never
see it; do not put it in the release notes.

`platform/3ds/tools/lod-link.py` presses buttons, holds the sticks, taps, takes screenshots of
both screens, runs GZDoom console commands (`map`, `give`, `save`, `load`...), reads and sets
cvars and reads state and memory (`platform/3ds/README.md`, "Console link"; `--help`). Say
before driving the game or installing while the owner is playing: an install loses unsaved
progress. If the connection is refused, ask for the address the developer overlay shows (DEVELOPER ->
OVERLAY WITH SELECT on, then SELECT in a level: STATUS box); do not scan the network without need. The first build with the server
arrives the old way (FBI / ftpd).
