# Release notes

One file per tag: `vX.Y.Z.md`. The release workflow copies it into the GitHub
release body between `<!-- lod-notes -->` markers, and the console's updater shows
it before the player installs.

Rules (details in `CLAUDE.md`, "Release notes"):

- 3 to 6 short lines, each starting with `- `. Plain text for the player, in
  English, under ~2.5 KB. The updater draws it in a 42-column text view: no
  tables, links or images.
- Every release, beta or stable, lists only what is new since the previous tag:
  the updater shows each release's notes between the installed version and the
  offered one, betas included, on both channels. (`v0.3.1` is the exception: players
  updating from `v0.2.4` have the older updater, which shows only the offered
  stable's block, so it lists everything since `v0.2.4`.)
- Written when the tag is made, as one summary of the range, and merged before
  the tag exists.

Example:

```
- Installs as its own app, next to the original port.
- New DEBUG page with an SD log and a perf recorder (betas only).
- Smoother overworld on the New 3DS.
```
