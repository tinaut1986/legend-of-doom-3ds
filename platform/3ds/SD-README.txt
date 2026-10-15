Legend of Doom 3DS
=================

tinaut1986's fork of Esteban PDN's port. It installs as its own app, next to
the original one, and keeps its files in its own folder.

Use BUILD-MANIFEST.txt from the same package to identify its version, build ID
and renderer profile.

INSTALLATION

For the Homebrew Launcher, copy the included "3ds" folder to the root of the
SD card, then start:
  sdmc:/3ds/Legend of Doom 3DS/legend-of-doom-3ds.3dsx

Keep the matching data folder with the executable. Downloading only a 3DSX
does not provide the required engine and game data.

For the CIA build, install with FBI. The CIA embeds Freedoom, Legend of Doom
and the matching GZDoom resources. Configuration, saves, logs and diagnostic
files are stored separately under:
  sdmc:/3ds/Legend of Doom 3DS/

Supported consoles: New Nintendo 3DS, New Nintendo 3DS XL and New Nintendo
2DS XL. The Old Nintendo 3DS and Nintendo 2DS families are not supported.
No NES ROM or commercial Doom IWAD is required.

A loading Triforce appears while the engine parses its data. Wait for the
normal title menu before starting a game.

AUDIO

Audio on a real console requires that console's own DSP firmware at:
  sdmc:/3ds/dspfirm.cdc

With Luma3DS, open Rosalina, choose "Miscellaneous options...", then
"Dump DSP firmware". This system file is not included in the package.

RENDERER AND DISPLAY

Releases use profile=hardware-hybrid: since v0.2.0 the PICA200 GPU draws the 3D
world (with stereoscopic 3D) and the 2D. Menus use native 400x240.
NovaGL does not render world geometry in this profile.

Packages marked profile=hardware-safe use the CPU-only 320x200 recovery path
and SDL/libctru presentation. This is the default source-build and CI profile.
Other NovaGL profiles are intended for renderer investigation.

Rendering is uncapped. Actual frame rate depends on the scene;
display refresh rate is not a measurement of gameplay FPS.

CONTROLS

Circle Pad: move / strafe
C-Stick: look
Touch: menus, map/items, and look when Touch or Both camera input is selected
A: use / interact; confirm in menus
B: back / cancel in menus
X: optional sprint at full health
Y: jump
L / R: alternate attack / attack
ZL / ZR: previous / next inventory item
D-Pad left / right: previous / next weapon
D-Pad down: use inventory item
D-Pad up: automap
START / SELECT: open the menu

Controller options provide camera mode, C-Stick sensitivity, the sprint toggle
and a control reference. The bottom-screen map and item panel support touch.

UPDATES

The CIA build has Update in the title and pause menus. Choose Stable for
stable releases, Beta for test builds. Save progress before installing: installation closes the game.
Saves and configuration remain on the SD card.

The 3DSX build does not install in-game updates. Replace its executable and
matching data together using a complete release ZIP.

BUG REPORTS

Press L + R + A while the issue is visible. Attach the complete quick folder
from:
  sdmc:/3ds/Legend of Doom 3DS/dumps/

For updater errors, also attach:
  sdmc:/3ds/Legend of Doom 3DS/update.log

L + R + X makes a full dump including process memory; use it only when needed
and review its contents before sharing. L + R + Y deletes diagnostic folders
only. It does not delete saves or configuration.

Downloads and reports:
  https://github.com/tinaut1986/legend-of-doom-3ds
Original port:
  https://github.com/EstebanPdN/legend-of-doom-3ds

Legend of Doom was created by DeTwelve Games. See CREDITS.md and
THIRD-PARTY-LICENSES.md; component notices are preserved under licenses/.
