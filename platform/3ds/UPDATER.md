# Built-in updater

Start opens the existing menu. Update sits between Save Game and Quit Game in
both the title menu and the pause menu (which retains Resume Game first).
The Update caption is copied from the engine's Options renderer, preserving its
font and red translation. Information uses NewSmallFont, like Controls. The
changelog stays on the top screen, with bundled notes available offline. Both
screens retain the font shading and dark outlines at the same scale. Release
notes use the dimmed TITLEPIC artwork, independently of the idle lore page or
paused scene, and wrap to the measured font width. The lower information and
actions are centered between the fixed Update heading and Back button.

Opening Update checks GitHub. The selected stable/beta channel persists
under `sdmc:/3ds/Legend of Doom 3DS/update/channel.txt`. A selects, B returns or
cancels a transfer, and the bottom screen supports touch. L/R pages through the changelog without leaving the update controls. Installation requires an explicit Yes; No is selected
by default. Installation closes the game, so save progress first.

CIA updates include the matching engine and game resources. The updater checks
HTTPS certificates, download size, SHA-256 and the CIA title ID before asking
AM to overwrite the installed title. It never deletes that title first. HOME,
sleep and menu cancellation are disabled during installation. Temporary
downloads are removed after completion/failure. Saves and configuration are
outside the CIA and are preserved. Errors are recorded in
`sdmc:/3ds/Legend of Doom 3DS/update.log`.

The 3DSX build displays `INSTALL THE CIA TO UPDATE`: replacing only its executable
would leave its external engine/game data stale.

## Release compatibility

The source is `tinaut1986/legend-of-doom-3ds` (`UPDATE_REPOSITORY`), via GitHub's
public releases API; this fork never reads EstebanPdN's releases. Drafts are
ignored. Stable is GitHub's normal releases, Beta its pre-releases (tags on a
`release/*` branch not yet merged into `main`; see `CLAUDE.md`, "Releases"). The
highest version in the selected channel is validated; an invalid newest asset is an
error, not a fallback to an older build.

Versions are this fork's `X.Y.Z` (tags `vX.Y.Z`). The installed build may also be
`X.Y.Z-dev.N[.M]+hash` (a build off a release branch) or a beta (the channel is
baked in, `LOD3DS_PORT_IS_BETA`); both sort below the plain `X.Y.Z`. A beta-channel
tag is compared as `X.Y.Z-beta`, so a beta never offers its own page while the
stable release of its number is newer. Upstream's `vX.Y` and `vX.Y-En` forms still
parse. Equal or older versions cannot be installed. Betas and dev builds start on
the Beta channel; stable builds on Stable.

The CIA asset must have GitHub's SHA-256 digest and be named
`legend-of-doom-3ds-vX.Y.Z.cia` (the release workflow publishes that name). The
download URL must belong to that exact repository, tag and asset; the CIA's title
ID must be this fork's `0x0004000005A2D100` (`LOD3DS_TITLE_ID`), never upstream's.
The handheld changelog shows the release body's `<!-- lod-notes -->` block (from
`docs/release-notes/<tag>.md`), or the `## Changelog` commit list when it is
missing. The updater reads up to 100 releases, limits metadata to 2 MiB and CIA
files to 128 MiB.

## Local verification

Build the native Jansson 2.14 source with CMake, then run (macOS or Linux; the
updater harness also needs the native libcurl headers):

```sh
python3 platform/3ds/tests/run-update-tests.py --jansson-prefix /path/to/native/jansson --live-check
```

The manifest harness uses AddressSanitizer and UndefinedBehaviorSanitizer.
The I/O harness exercises transfer bounds, real host SHA-256, corrupt/truncated
files, cancellation, wrong title, insufficient space and partial install writes.
AM is mocked; this does not prove installation on a physical console.
The optional live check uses the bundled CA to query both GitHub channels.

The build accepts `LOD3DS_SCRIPT_VALIDATOR` pointing to a native executable built
from this tree. Its `gzdoom.pk3` and `game_support.pk3` must be built and placed
next to that executable before running the check.

On a New 3DS, test Update from title and pause, both channels, B/Start/touch/L/R,
no Wi-Fi, cancellation, and returning to gameplay. A channel without a public release displays
`NO STABLE RELEASE YET` / `NO BETA RELEASE`.
Installing a newer release must still be checked on hardware once a suitable
release exists. Creating this build does not publish anything.

Upstream's history (0.47, v0.8-E3, v1.0) describes EstebanPdN's app; this fork
starts at 0.1.0 with its own title ID and folder, so it never updates or is updated
by the upstream app.

The updater initializes the SSL service used by the 3DS mbedTLS entropy
callback, declares its CIA access/dependency, and closes it after curl cleanup.
Network work runs one priority step above its caller (bounded at 0x18) so the
Core 0 renderer cannot starve the TLS handshake. Connection setup allows 30
seconds and release metadata allows 90 seconds. TLS verification remains on.
Setup failures and curl errors are distinguished on screen, with details in
update.log.
