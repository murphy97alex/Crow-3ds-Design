# Pixel UI Designer (3DS)

Homebrew pixel-art / UI drawing app for the New 3DS, written in C with
libctru + citro2d. See [docs/BRIEF.md](docs/BRIEF.md) for the plan.

## Controls (Milestone 1)
| Input | Action |
|---|---|
| Stylus on canvas | Draw with the current tool |
| Tap a palette swatch | Pick that color; tap the selected one again to edit it (wheel, brightness bar, hex) |
| Hold R + stylus | Eyedropper (also available as a tool) |
| L / ZL | Undo / redo |
| D-pad left/right | Previous / next tool (pencil, eraser, eyedropper) |
| D-pad up/down | Zoom in / out |
| Circle pad | Pan |
| X | Grid on/off (shown at zoom 4x and up) |
| Y | Switch to last-used tool |
| SELECT | Top screen: tools/info ↔ big preview at 1x, 2x, 3x… |
| START | Menu: new canvas (presets + custom), save, save as, open, exit |

Projects are saved to `/3ds/pixel-ui-designer/projects/*.pxd` ([format](docs/PXD.md)).

## Install
Grab `pixel-ui-designer.cia` from the latest
[release](../../releases/latest) and scan its download URL with FBI's
*Remote Install → Scan QR Code*. Or copy the `.3dsx` to `/3ds/` on the SD
card and launch it from the Homebrew Launcher.

## Build
Needs devkitPro with `3ds-dev` (devkitARM, libctru, citro2d).

```sh
make          # .3dsx
make cia      # .cia (needs bannertool + makerom on PATH; scripts/fetch-tools.sh gets them)
```

CI builds every push; pushing a `v*` tag publishes a GitHub release with both files.

## License
MIT. Icon, banner and banner sound are original placeholders generated for this repo.
