# Nintendo 3DS build guide

The published stable release is [v1.0](https://github.com/EstebanPdN/legend-of-doom-3ds/releases/tag/v1.0). It targets New Nintendo 3DS, New Nintendo 3DS XL and New Nintendo 2DS XL, and uses `hardware-hybrid`. See the [main README](../../README.md) for installation and player controls.

## Requirements

- devkitPro/devkitARM, libctru and the devkitPro 3DS CMake toolchain.
- CMake, Python 3, Git, cURL, UnZip and a native C/C++ toolchain.
- SDL2 development files for the native host tools (`libsdl2-dev` in Linux CI).
- `makerom` and `bannertool` for CIA packaging. Pinned Linux x86_64 binaries are downloaded when needed. On macOS, supply native tools; `makerom-macos` and `bannertool-macos` are detected in the tools directory.

Dependency revisions and checksums are pinned in [dependencies.sh](dependencies.sh). The scripts fetch SDL2, ZMusic, minimp3, OpenAL Soft/NDSP, NovaGL, Freedoom and Legend of Doom, then apply the 3DS patches. Updater dependencies are described in [update-dependencies/README.md](update-dependencies/README.md).

## Build profiles

| Profile | Purpose |
|---|---|
| `hardware-hybrid` | Published v1.0 renderer: SoftPoly world rendering on CPU0/CPU2, OpenAL/NDSP audio and a bounded PICA200 texture presenter. |
| `hardware-safe` | Default build and GitHub Actions profile. CPU-only recovery renderer at 320×200 with SDL/libctru presentation and OpenAL/NDSP audio. |
| `release` | Legacy NovaGL world-renderer experiment. The name does not mean this is the published v1.0 profile. |
| `hardware-candidate` | NovaGL hardware investigation with audio, telemetry and automatic MAP01 startup. |
| `hardware-diagnostic` | The same diagnostic world-renderer path with audio disabled. |

Build the renderer profile used by the published release:

```sh
./platform/3ds/build.sh hardware-hybrid
```

Build the default recovery profile:

```sh
./platform/3ds/build.sh
```

A profile can also be selected through the environment:

```sh
LOD3DS_BUILD_PROFILE=hardware-hybrid ./platform/3ds/build.sh
```

The profile argument takes precedence over `LOD3DS_BUILD_PROFILE`.

The version comes from git ([tools/version.sh](tools/version.sh)): `X.Y.Z` on a `vX.Y.Z` tag, `X.Y.Z-dev.N[.M]+hash` on or off `release/vX.Y.Z`. The upstream port's version this fork is based on is [upstream_version.txt](upstream_version.txt). Both, the channel, the debug-tools switch and the build ID go into the generated `build-3ds/game/generated/lod3ds_version.h`, which only a few files include, so a new commit rebuilds those files and not the engine.

`./build_3ds.sh` at the repository root is a build assistant around this script: a menu, or flags (`--profile`, `--mode debug|prod`, `--ftp IP`), and an upload of the CIA to the console's FTP server (ftpd or FBI, port 5000) as `cias/legend-of-doom-3ds-<version>.cia`.

## Configuration and outputs

| Variable | Purpose / default |
|---|---|
| `DEVKITPRO` | devkitPro root; `/opt/devkitpro`. |
| `DEVKITARM` | devkitARM root; `$DEVKITPRO/devkitARM`. |
| `LOD3DS_BUILD_ROOT` | Out-of-tree build directory; `build-3ds` at the repository root. |
| `LOD3DS_JOBS` | Parallel build jobs; `4`. |
| `LOD3DS_VERSION` | Overrides the version from git (the release workflow sets it). |
| `LOD3DS_CHANNEL` | `release`, `beta` or `dev` (default): baked into the binary for the updater and the version label. |
| `LOD3DS_DEBUG_TOOLS` | `1`/`0`: debug tools (PLAN Phase 1); default `1` except on the `release` channel. |
| `LOD3DS_TOOLS_ROOT` | Directory with packaging tools; `../Tools/bin` relative to the repository. |
| `MAKEROM`, `BANNERTOOL` | Explicit packaging executable paths. |
| `LOD3DS_OPENAL_SOURCE_DIR` | Optional existing OpenAL Soft/NDSP checkout. |
| `LOD3DS_SKIP_CIA=1` | Build the 3DSX and SD ZIP without the CIA. |
| `LOD3DS_SCRIPT_VALIDATOR` | Optional native engine executable for the packaged game-script check. |

The distribution directory is `$LOD3DS_BUILD_ROOT/dist/`, or `build-3ds/dist/` by default. Local builds use a stem such as:

```text
legend-of-doom-3ds-v1.0-hardware-hybrid-<12-digit-build-id>
```

The generated files include `.3dsx`, `.cia` (unless skipped), `-sd.zip`, `-debug-symbols.zip`, `BUILD-MANIFEST.txt` and `SHA256SUMS.txt`. Public release assets use the shorter filenames linked from the main README.

The CIA embeds the matching engine and game data in RomFS. The 3DSX needs the matching SD ZIP's `3ds/Legend of Doom 3DS/data/` directory. Configuration, saves, logs and dumps use `sdmc:/3ds/Legend of Doom 3DS/` for both formats. The console's DSP firmware must be dumped separately to `sdmc:/3ds/dspfirm.cdc`.

## Renderer and runtime

The hybrid profile renders gameplay at 320×192 by default, with a configurable 50–100% render scale in 5% steps. Menus use native 400×240. CPU0 and CPU2 perform SoftPoly raster work; scene traversal stays single-owner and the audited desktop BSP worker is disabled on 3DS. OpenAL/NDSP handles audio on CPU1. PICA200 receives a completed frame texture rather than world geometry. NovaGL is linked but not initialized in this profile.

Rendering is uncapped and display-synchronized; Doom simulation remains at 35 Hz. A 60 Hz LCD does not establish a 60 FPS gameplay result. Record physical-console measurements separately from host and emulator results, and include the build ID, profile and display settings.

The CPU-only recovery profile uses a 320×200 canvas mapped to the top LCD through SDL/libctru linear framebuffers. It does not initialize NovaGL or Citro3D. Both CPU-renderer profiles start at the normal title menu, preserve SD saves/configuration and provide the diagnostic button chords below.

For the built-in CIA updater, its release format, TLS checks, host harness and console test procedure, see [UPDATER.md](UPDATER.md).

## Diagnostics and verification

| Button chord | Operation |
|---|---|
| `L + R + A` | Quick diagnostic capture: both screens, engine state, sanitized configuration, logs and build identity; no full RAM payload. |
| `L + R + X` | Full diagnostic capture including readable memory mappings. Use only when needed; it can exceed 130 MiB. |
| `L + R + Y` | Delete the diagnostic folders under `sdmc:/3ds/Legend of Doom 3DS/dumps/`. Saves and configuration are outside this cleanup. |

Attach the complete quick folder for ordinary bug reports. For updater failures, include `sdmc:/3ds/Legend of Doom 3DS/update.log`. Keep the matching manifest and debug symbols for address resolution; review full memory dumps before sharing them.

Useful local checks:

```sh
git diff --check
./platform/3ds/test-patches.sh
```

The build script also validates the patch files, banner structure and banner audio. GitHub Actions checks clean source, pinned patch application, package contents, public-file boundaries and output checksums, and builds `hardware-safe`. A successful CI build establishes package/build checks; gameplay and performance still require console evidence.

## Console link (debug builds)

Debug builds (`LOD3DS_DEBUG_TOOLS=1`: dev builds and betas) carry an FTP server inside the game, on port 5000, switched on with **OPTIONS -> DEVELOPER -> FTP SERVER**. The setting is saved, so the server comes back after a relaunch; with no Wi-Fi it retries every two seconds. Its address shows in the developer overlay's STATUS box (DEVELOPER -> OVERLAY WITH SELECT, then SELECT in a level), and `build_3ds.sh` finds it by scanning the LAN. It is ported from `../mzm` (`ftp_server_3ds.c`, `debug_link_3ds`, `cia_install_3ds.c`, `net_3ds.c` under `src/common/platform/3ds/`).

- **Files.** The whole SD card is `/`; the game's folder is `/3ds/Legend of Doom 3DS/`. Login in `src/common/platform/3ds/ftp_credentials_3ds.h` (also in `CLAUDE.md`), one client at a time, passive mode. FileZilla, WinSCP, curl and lftp work.
- **Install over the running game.** `SITE INSTALL <path>` installs a CIA of this title from the SD card and relaunches into it; a toast on the touch screen says so meanwhile. `./build_3ds.sh --install [IP]` builds, uploads to `update/dev.cia` and sends the command.
- **Remote control.** `platform/3ds/tools/lod-link.py` drives the game through `SITE` commands: `state`, `keys`/`press` (buttons), `stick` (Circle Pad / C-Stick), `touch`, `shot` (both screens as PNG), `console` (any GZDoom console command: `map`, `give`, `save`...), `cvar`, `dump`, `peek`/`poke` (by address or ELF symbol), `get` and `run FILE`. `lod-link.py --help` lists them.

- **Perf recorder.** `lod3ds_perf start|stop|status` (console command, debug builds) records one row per presented frame and writes `debug/perf-NNNN.csv` on stop: interval and the time of each part (logic tics, 3D view, SoftPoly raster wait, 2D, compositor, PICA200 present, limiter, bottom screen, the rest), game state, menu and canvas size (`src/common/platform/3ds/perf_3ds.h`). `platform/3ds/tools/perf-analyze.py FILE --by-state` summarises it; `lod-link.py perf SECONDS` does all three from the PC.

Stable builds have no FTP server unless `dev-ftp.txt` exists in `/3ds/Legend of Doom 3DS/` (created over FTP while a debug build is installed): then they start an install-only server that reaches only `update/` and has no remote control, enough for `--install`.

## Historical notes

The earlier version-by-version implementation notes are preserved in [history/README-before-v1.0.md](history/README-before-v1.0.md). The [August technical roadmap](../../docs/history/ROADMAP-2026-08-27.es.md), [v0.31 review](CODE-REVIEW-v031.es.md), [performance audit](PERFORMANCE-AUDIT.es.md) and versioned fix notes describe their original candidates and measurements. Use this guide and the published build manifest for the current release profile.
