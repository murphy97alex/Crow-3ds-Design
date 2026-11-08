# Pixel UI Designer (3DS)

Homebrew pixel-art / UI drawing app for the New 3DS, written in C with
libctru + citro2d. See [docs/BRIEF.md](docs/BRIEF.md) for the plan.

## Controls
| Input | Action |
|---|---|
| Stylus on canvas | Use the current tool |
| Tap a swatch | Draw with that color; tap the selected one again to edit it (color square + hue bar, hex, or **Palette text** to type all swatches). Editing a swatch never changes the drawing |
| START → Linked swatches | Per project. On: editing a swatch also recolors every pixel of that color (like Aseprite's indexed mode). Off by default |
| Hold R + tap a swatch | Save the current color (e.g. from the eyedropper) into that swatch |
| A | Tool options (size 1–9, brush Square/Round/Dither, pixel-perfect, outline/filled, fill mode, mirror axis) |
| D-pad up/down | Previous / next tool: Pencil, Eraser, Fill, Line, Rectangle, Ellipse, Select, Stamp, Eyedropper. Last entry **Mirror** opens Mirror On/Off + axis, which applies to all drawing tools and stamps |
| Select tool | Drag to select; drag inside to move; A = actions (copy, cut, paste, delete, flip, rotate, outline, save as stamp); B deselects |
| Stamp tool | A: pick a stamp, Size Original or Drag to size. Original: touch to place, drag to position. Drag to size: drag a box to scale it (ZR keeps proportions); a tap places it at original size |
| Hold R + stylus | Eyedropper |
| Hold ZR while dragging a shape | Snap: square/circle; lines to 0°, 45°, 90° and 2:1 |
| B while dragging a shape | Cancel the shape |
| L / ZL | Undo / redo |
| D-pad right/left | Zoom in / out (¼x to 24x) |
| B | Toggle eraser (back to your previous tool on the next press). In Select it deselects; while dragging a shape it cancels |
| Circle pad | Pan |
| X | Grid on/off (zoom 4x and up) |
| Y | Layers menu (coming with the Layers update) |
| SELECT | Top screen: tools/info ↔ big preview at 1x, 2x, 3x… |
| START | Menu: new canvas, save, save as, open, export PNG, shift canvas (wrap), exit |

Projects are saved to `/3ds/pixel-ui-designer/projects/*.pxd` ([format](docs/PXD.md)),
stamps to `/3ds/pixel-ui-designer/stamps/`, and PNG exports to `/3ds/pixel-ui-designer/export/`
(named `<project>_<W>x<H>.png`) — grab them with ftpd.

## Install
In FBI: *Remote Install → Scan QR Code*, then scan this. It always points at the
newest release's `.cia`.

![Install QR](docs/install-qr.png)

Link: https://github.com/murphy97alex/Crow-3ds-Design/releases/latest/download/pixel-ui-designer.cia
 Or copy the `.3dsx` to `/3ds/` on the SD
card and launch it from the Homebrew Launcher.

## Build
Needs devkitPro with `3ds-dev` (devkitARM, libctru, citro2d).

```sh
make          # .3dsx
make cia      # .cia (needs bannertool + makerom on PATH; scripts/fetch-tools.sh gets them)
```

CI builds every push; pushing a `v*` tag publishes a GitHub release with both files.

## License
MIT. The default palette is CC0 ([details](docs/PALETTE.md)). Icon, banner and banner sound are original placeholders generated for this repo.
