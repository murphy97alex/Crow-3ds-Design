# Pixel UI Designer (3DS)

Homebrew pixel-art / UI drawing app for the New 3DS, written in C with
libctru + citro2d. See [docs/BRIEF.md](docs/BRIEF.md) for the plan.

## Home screen
The app opens on **Home** (also START → Home). START on Home exits the app:
- **New** tab: tap a canvas size twice (or A) to start, or Custom.
- **Gallery** tab: thumbnails of saved projects, newest first (a recovered autosave shows first).
  Tap to select, tap again or A to open; drag to scroll. Buttons: Open, Rename, Copy, Delete, Export
  (PNG with your export sizes). X deletes, Y renames, L/R switch tabs, B goes to the canvas.

## Controls
| Input | Action |
|---|---|
| Stylus on canvas | Use the current tool |
| Tap a swatch | Draw with that color; tap the selected one again to edit it (color square + hue bar, hex, or **Palette text** to type all swatches). Editing a swatch never changes the drawing |
| START → Linked swatches | Per project. On: editing a swatch also recolors every pixel of that color (like Aseprite's indexed mode). Off by default |
| Hold R + tap a swatch | Save the current color (e.g. from the eyedropper) into that swatch |
| A | Tool options (size 1–9, brush Square/Round/Dither, pixel-perfect, outline/filled, fill mode, mirror axis) |
| D-pad up/down | Previous / next tool: Pencil, Eraser, Fill, Shade, Line, Rectangle, Ellipse, Select, Stamp, Text, Eyedropper. Last entry **Mirror** opens Mirror On/Off + axis, which applies to all drawing tools and stamps |
| Select tool | Mode (A): Rectangle, Magic wand (connected or all of a color), Lasso. Drag inside a selection to move it. The selection stays when you switch tools, and drawing tools then only paint inside it. Copy/paste work across projects. A = actions (copy, cut, paste, delete, flip, rotate, outline, crop canvas to selection, save as stamp); B deselects |
| Fill tool | Mode (A): Connected area, Replace color, or Gradient: drag to fade the current color into the area with a 4x4 dither |
| Shade tool | Paint over pixels to step them one swatch lighter (right) or darker (left); A sets direction and brush |
| Text tool | Tap the canvas and type (multiple lines OK). Drag to move, drag the corner tab to resize (1-32x, stays crisp), tap the text to retype, B or tap outside to place. A: font (6, see [fonts](docs/FONTS.md)), size 1-32x, spacing, outline color |
| Stamp tool | A: pick a stamp, Size Original or Drag to size. Original: touch to place, drag to position. Drag to size: drag a box to scale it (ZR keeps proportions); a tap places it at original size |
| Hold R + stylus | Eyedropper |
| Hold ZR while dragging a shape | Snap: square/circle; lines to 0°, 45°, 90° and 2:1 |
| B while dragging a shape | Cancel the shape |
| L / ZL | Undo / redo |
| D-pad right/left | Zoom in / out (¼x to 24x) |
| B | Toggle eraser (back to your previous tool on the next press). In Select it deselects; while dragging a shape it cancels |
| Circle pad | Pan |
| X | Grid on/off (zoom 4x and up) |
| Y | Layers: up/down pick, X show/hide, left/right opacity, L/R move, A = new layer/folder, duplicate, delete, move, merge down/visible, lock, rename, collapse folder |
| Bottom row of the palette strip | Recent colors (tap to reuse) |
| SELECT | Top screen: tools/info ↔ big preview at 1x, 2x, 3x… |
| START | Menu: Home (new / gallery), save, save as, import PNG, export PNG, canvas size (with anchor), shift layer, Colors (ramp between swatches, adjust hue/saturation/brightness/invert), View (flip preview, 3x3 tile preview, HUD grid, isometric grid, autosave), linked swatches, exit |

Projects are saved to `/3ds/pixel-ui-designer/projects/*.pxd` ([format](docs/PXD.md)),
stamps to `/3ds/pixel-ui-designer/stamps/`, PNG exports to `/3ds/pixel-ui-designer/export/`,
and PNGs to import go in `/3ds/pixel-ui-designer/import/`
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
MIT. The default palette is CC0 ([details](docs/PALETTE.md)). Fonts: see [docs/FONTS.md](docs/FONTS.md)
(Tom Thumb, PICO-8, Kenney Pixel and the Crow fonts are CC0; Spleen is BSD 2-Clause,
[license](fonts/LICENSE-spleen.txt)). Icon, banner and banner sound are original placeholders generated for this repo.
