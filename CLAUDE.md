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
| `src/common/platform/3ds/` | EstebanPdN, now ours | The 3DS platform layer: input, lifecycle, diagnostics (L+R+A/X/Y), bottom screen, the SoftPoly → PICA200 presenter, and the CIA updater (`update/`). |
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
  `platform/3ds/upstream_version.txt` (P0.1) to the upstream version merged. Never
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

- Upstream's version is a plain number in `platform/3ds/version.txt` (`1.0`), and its
  tags have two components (`v0.31`, `v1.0`, `v1.1`) or an experiment suffix
  (`v0.8-E3`). Those tags are in this repository too. **Only three-component tags
  are ours**; never create another kind.
- P0.1 replaces `version.txt` with a version derived from git, as in
  `../zelda-tmc-3ds` (`platform/3ds/tools/version.sh`): an exact `vX.Y.Z` tag gives
  `X.Y.Z`; anything on (or cut from) `release/vX.Y.Z` gives
  `X.Y.Z-dev.<commits since main>[.<commits since the release branch>]+<hash>`.
  Upstream's base moves to `platform/3ds/upstream_version.txt` and is shown next to ours.
- This fork will install as **its own application** (P0.1): its own title ID
  (upstream is `0x5A2D0`, `platform/3ds/cia/legend-of-doom-3ds.rsf`; the updater checks
  it in `src/common/platform/3ds/update/updater.c`), name and data folder, so it never
  touches the upstream app's `sdmc:/3ds/legend-of-doom/` and both can be installed
  side by side.

### Releases (installer on GitHub)

Until P0.1 lands there is no release workflow: `.github/workflows/build-3ds.yml` only
builds the `hardware-safe` profile on pushes to `main` and on PRs, and uploads
artifacts. The target, as in `../zelda-tmc-3ds`:

`.github/workflows/build-release.yml` builds the CIA on any `vX.Y.Z` tag push (and on
manual dispatch) and publishes a GitHub Release with it and a QR code. The channel
depends on whether `main` can reach the tagged commit:

| Built from | Channel |
|---|---|
| Tag on an unmerged `release/*` branch | `Beta` (pre-release) |
| Tag on `main`, or on a commit merged into `main` | `Release` |
| Manual dispatch on any branch | `Beta` |

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

The release workflow passes the channel (`beta` or `release`) to the build, which
writes it into a generated header next to the version, so the updater knows whether
the installed build is a beta: an installed beta is older than the stable release of
its own number, and is never offered its own pre-release page. The exact names land
with P0.1; document them here then.

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
./platform/3ds/build.sh hardware-hybrid    # the renderer upstream's v1.0 ships
./platform/3ds/build.sh                    # hardware-safe, the CPU-only recovery profile (CI)
./platform/3ds/test-patches.sh && git diff --check   # what CI checks before building
python3 platform/3ds/tests/run-update-tests.py ...   # updater host tests (UPDATER.md)
```

Outputs go to `build-3ds/dist/`: `.3dsx`, `.cia`, `-sd.zip`, `-debug-symbols.zip`,
`BUILD-MANIFEST.txt`, `SHA256SUMS.txt`. The profiles are described in
`platform/3ds/README.md`; `release` is a legacy NovaGL experiment despite its name.

Diagnostics already in upstream (all builds): **L+R+A** quick capture, **L+R+X** full
capture with memory, **L+R+Y** delete captures, into `sdmc:/3ds/legend-of-doom/dumps/`
(`src/common/platform/3ds/diagnostics_3ds.cpp`); updater log in `update.log`.

Test on hardware whenever performance is involved; Azahar is fine for logic but says
nothing about real timings. Upstream supports only the New 3DS family; this fork's
goal (PLAN) is New 3DS and Old 3DS/2DS with stereo 3D, so the owner has both a New 3DS
and a 2DS to test on.

## The console in the loop (for Claude)

Not available yet. PLAN Phase 1 brings `../zelda-tmc-3ds`'s FTP server, install over
FTP with relaunch, and the remote link (`tools/tmc-link/`) to this port; document them
here when they land. Until then the owner installs builds and sends back dumps.
