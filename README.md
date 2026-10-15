# Legend of Doom 3DS

<img width="1672" height="941" alt="Legend of Doom 3DS" src="https://github.com/user-attachments/assets/a84919c8-ce45-4549-b2d3-89fdf45f2999" />

[![Latest release](https://img.shields.io/github/v/release/tinaut1986/legend-of-doom-3ds?include_prereleases&label=latest%20release)](https://github.com/tinaut1986/legend-of-doom-3ds/releases)
[![Build Nintendo 3DS packages](https://github.com/tinaut1986/legend-of-doom-3ds/actions/workflows/build-3ds.yml/badge.svg)](https://github.com/tinaut1986/legend-of-doom-3ds/actions/workflows/build-3ds.yml)

> A fork of [EstebanPdN's port](https://github.com/EstebanPdN/legend-of-doom-3ds) of
> [Legend of Doom](https://github.com/emawind84/legend-of-doom), the first-person GZDoom
> conversion of the original *The Legend of Zelda* by DeTwelve Games. It has its own versions
> and releases, and installs as its own app next to the original port.

**[v0.2.0 is the latest stable release](https://github.com/tinaut1986/legend-of-doom-3ds/releases/tag/v0.2.0)**, for **New Nintendo 3DS, New 3DS XL and New 2DS XL**. Old Nintendo 3DS / 2DS support is in progress (see [docs/PLAN.md](docs/PLAN.md)) and is not in a release yet.

The port uses GZDoom 4.7.1 and Freedoom: Phase 2. No NES ROM or commercial Doom IWAD is required.

## What this fork adds

- The 3D world is drawn by the PICA200 GPU: about 60 fps on the New 3DS.
- Stereoscopic 3D with the 3D slider.
- A bottom screen in the style of the NES pause screen: items, map and hearts.
- Menus in English and Spanish.
- An in-game updater that offers new versions at start.
- Fixed the out-of-memory close after finishing a dungeon (#4): a memory leak on every map load (`ibo_data`), texture mipmaps built while mipmapping was off, and lighter map loading.

Release notes: [docs/release-notes](docs/release-notes/). Roadmap and open work: [docs/PLAN.md](docs/PLAN.md).

## Download and installation

Download the CIA from [GitHub Releases](https://github.com/tinaut1986/legend-of-doom-3ds/releases/latest).

| v0.2.0 download | Use |
|---|---|
| [CIA](https://github.com/tinaut1986/legend-of-doom-3ds/releases/download/v0.2.0/legend-of-doom-3ds-v0.2.0.cia) | Install with FBI. Includes the matching game and engine data. |
| [Build manifest](https://github.com/tinaut1986/legend-of-doom-3ds/releases/download/v0.2.0/BUILD-MANIFEST.txt) / [SHA-256 checksums](https://github.com/tinaut1986/legend-of-doom-3ds/releases/download/v0.2.0/SHA256SUMS.txt) | Build identity, component revisions and file verification. |

Install the CIA with FBI, or open **Remote Install → Scan QR Code** and scan:

<img src="https://github.com/tinaut1986/legend-of-doom-3ds/releases/download/v0.2.0/QR-v0.2.0.png" width="220" alt="FBI installation QR code for Legend of Doom 3DS v0.2.0" />

The app uses title ID `0x5A2D1`, so it does not replace the original port. Configuration, saves and diagnostic files are stored under `sdmc:/3ds/Legend of Doom 3DS/`.

### Audio setup

Audio on a real console requires its own DSP firmware at `sdmc:/3ds/dspfirm.cdc`. With Luma3DS, open the Rosalina menu, choose **Miscellaneous options...**, then **Dump DSP firmware**. This system file is not distributed with the port.

## Features

- New menus drawn natively for the bottom screen: options change in place (button or touch) with no nested submenus, in English and Spanish.
- NES-style bottom screen during play: items, map and hearts.
- Circle Pad movement and configurable C-Stick / touch camera input.
- Save/load menus with previews and the native Nintendo 3DS keyboard.
- Music and sound effects through OpenAL Soft/NDSP.
- Display options for render resolution and draw distance.
- A built-in CIA updater with stable and beta channels.
- Quick diagnostic capture for bug reports.

## Controls

The in-game control reference is authoritative if it differs from this table.

| Nintendo 3DS input | Action |
|---|---|
| Circle Pad | Move / strafe |
| C-Stick | Look |
| Touch screen | Look when camera input is Touch or Both; select menu rows and items |
| A | Use / interact; confirm in menus |
| B | Back / cancel in menus |
| X | Sprint at full health (optional) |
| Y | Jump |
| L / R | Alternate attack / attack |
| ZL / ZR | Previous / next inventory item |
| D-Pad left / right | Previous / next weapon |
| D-Pad down | Use inventory item |
| D-Pad up | Automap |
| START / SELECT | Open the menu |

Controller options include C-Stick sensitivity, C-Stick/Touch/Both camera input, the full-health X sprint toggle and a control reference.

## Updating

Open **Update** from the title or pause menu, or accept the offer shown at start. Save your progress before installing an update, because installation closes the game. Saves and configuration remain on the SD card. See the [updater guide](platform/3ds/UPDATER.md) for details and troubleshooting.

## Bug reports

[Open a bug report](https://github.com/tinaut1986/legend-of-doom-3ds/issues/new?template=bug_report.yml) with your version, console model, installation type and steps to reproduce the problem.

While the issue is visible, press **L + R + A** and attach the complete quick diagnostic folder from:

```text
sdmc:/3ds/Legend of Doom 3DS/dumps/
```

For updater errors, also attach `sdmc:/3ds/Legend of Doom 3DS/update.log`.

## Building from source

Install devkitPro/devkitARM, libctru, 3ds-cmake, CMake, Python 3, Git, cURL and UnZip, plus a native C/C++ compiler and SDL2 development files for the host tools. CIA packaging also uses `makerom` and `bannertool`.

Build the renderer profile used by the published releases:

```sh
./platform/3ds/build.sh hardware-hybrid
```

Calling the script without a profile builds the `hardware-safe` CPU-only recovery profile. GitHub Actions also builds that recovery profile; its artifacts are separate from the published releases. Output packages are written to `build-3ds/dist/` by default.

See the [3DS build guide](platform/3ds/README.md) for all profiles, dependencies, package names and checks.

## Credits

- [DeTwelve Games](https://youtube.com/DeTwelveGames) — creator of Legend of Doom
- [GZDoom contributors](https://github.com/ZDoom/gzdoom) — engine
- [Freedoom contributors](https://github.com/freedoom/freedoom) — Phase 2 IWAD
- [NovaGL contributors](https://github.com/efimandreev0/NovaGL) — experimental OpenGL-to-Citro3D bridge
- SDL, ZMusic, OpenAL Soft/NDSP, Citro2D/Citro3D, devkitPro and libctru contributors — platform stack
- [Esteban PDN](https://github.com/EstebanPdN/legend-of-doom-3ds) — original Nintendo 3DS port this fork is based on
- tinaut1986 — this fork

See [CREDITS.md](CREDITS.md) and [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md) for additional notices.

## License and game content

The GZDoom-derived source is distributed under **GNU GPL v3.0**. See [LICENSE](LICENSE) for the complete terms. Third-party code and data retain their component-specific terms and notices in [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md). The engine license does not relicense the game data.

Each release tag provides its corresponding source through GitHub's source archives.

Legend of Doom's upstream repository does not declare a general open-source license. Its data is downloaded during the build and is not committed here; the original credits and component notices are preserved in the packages.

*The Legend of Zelda* and related names and content are owned by Nintendo. *Doom* and related names are owned by id Software and their respective rights holders. This unofficial fan project is not affiliated with or endorsed by Nintendo, id Software or Bethesda.
