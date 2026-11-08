# 3DS Pixel UI Designer — Brief

Track: Delegated (Claude Code builds, Alex directs from his phone).
Goal: Homebrew drawing app on Alex's New 3DS XL for sprites and interface visuals for Crow and other robots.
Main use: icons first. UI design (e.g. a HUD for a robot controller) is a plus.
Workflow rule: Pause and ping Alex for design choices, then continue after he decides.
Repo: public, MIT. No secrets/keys, only open-licensed fonts/assets, no Nintendo assets. Alex's art stays out of this repo.

## Layout
- Top screen (400x240, not touch): tool select + true-size preview
- Bottom screen (320x240, touch): drawing canvas
- Buttons: commands
- A (in drawing tools): tool options panel on the top screen (D-pad to change, A/B to close)

## Button layout (draft, not locked)
Stylus in right hand, frequent actions on left.
- Touch: draw | L: undo | ZL: redo
- D-pad L/R: prev/next tool | Up/Down: zoom
- Circle pad: pan | R (hold): eyedropper
- A: tool options panel (drawing tools); confirm/place stamp (select/stamp mode) | B: cancel/deselect
- ZR (hold while dragging a shape): snap — square/circle, lines to 0/45/90° and 2:1 iso
- X: grid toggle | Y: last-used tool
- Select: top screen tools <-> preview | Start: menu
- Settings: left-handed mode mirrors layout

## v1 features
- Drawing tools (set TBD)
- Text tool with open-licensed pixel fonts
- Saves (project files + PNG export)
- Canvas presets: 16, 32, 64, 128x64, 128, 240, 320x240, custom
- Stamps (save a selection, reuse it)
- Camera mode: snap photo, stylized filters (posterize, dither, 1-bit, pixelate, scanlines, RGB split, neon edge, duotone), then cut/edit into a stamp or the canvas. Cameras are VGA.

## v2 — Sprite animator
- Frame strip, onion skin, top-screen playback with FPS, PNG sprite sheet (+GIF maybe)
- BUILD RULE FOR v1: project file format must support multiple frames from day one.
- Out of scope: tweening timelines, bones.

## Tech
- C, devkitPro, libctru, citro2d
- GitHub Actions builds; releases installed via FBI QR
- PNG exports pulled off SD with ftpd

## Decisions (2026-11-07)
- **v1 tools:** pencil, eraser, flood fill; line, rectangle, ellipse (outline or filled);
  rectangle select + move, copy/paste, flip/rotate 90° (also the basis for stamps);
  mirror drawing (horizontal/vertical symmetry) — a **Mirror** entry at the end of the tool
  list opens On/Off + axis; when on it applies to every drawing tool and stamps (2026-11-08). Eyedropper stays on hold-R.
- **Buttons:** use the draft layout above for v1; left-handed mirror in Settings.
- **Colors:** Photoshop/Clip Studio style (changed 2026-11-08 after testing). You draw with a
  current color; the 32 swatches are saved colors edited with the square + hue bar picker, hex,
  or palette text. Editing a swatch never changes the drawing. Pixels index a per-project color
  table (up to 255 colors). Eyedropper picks any canvas color; R + tap a swatch saves the current
  color into it. Recolor on purpose with Fill → Replace color, or turn on **Linked swatches**
  (START menu, per project) so editing a swatch recolors matching pixels. PNG export is full color.
- **Crow display:** unknown for now. Design for full color; display-specific export later.
- **Project file:** custom binary `.pxd`, one file per project: versioned header, canvas size,
  palette, then a list of frames (compressed indexed pixels). Room to add layers later.
  PNG export is separate.
- **Default palette:** our own 32 colors, released CC0 (docs/PALETTE.md).
- **Palette text:** type/paste a list of hex codes to fill palette slots 1, 2, 3… at once.

## Roadmap
- **M0 (done, v0.0.1):** scaffold, CI, releases.
- **M1 (done, v0.1.0):** canvas, zoom/pan/grid, palette + hex/wheel picker, eyedropper, undo/redo,
  .pxd save/open, size presets.
- **M2 (done, v0.2.0):** brush sizes 1–9 with Square/Round/Dither shapes (pencil, eraser); pixel-perfect pencil
  (size 1); fill (contiguous or replace color); line, rectangle, ellipse (outline/filled, ZR snap,
  2:1 iso lines); mirror drawing (off / left-right / top-bottom / both); A tool options panel.
- **M3 (done, v0.3.0–0.3.2):** select + move, copy/paste, flip/rotate 90°; stamps; outline (1px around selection/art);
  shift/wrap canvas; PNG export: one checklist of sizes (1x–8x and 16–256 px) with a
  "Recommended" preset (1x/2x/4x/8x). Follow-ups from testing: swatches don't recolor (optional
  linked swatches), zoom to 1/4x, Mirror for all tools, stamps drag-to-size, CC0 default palette,
  palette text, square + hue bar picker.
- **Proposed order from here (Alex picks each milestone):**
  - **Canvas & import:** resize/crop canvas (anchor, crop to selection); import PNG from SD
    (as project, stamp, or palette); magic wand (select by color); lasso selection.
  - **Color tools:** color ramp between two swatches; adjust colors (hue/saturation/brightness,
    invert); gradient dither fill; recent colors row; shading tool.
  - **View & guides:** flip view (mirrored preview); HUD layout grid (8/16 px); isometric 2:1 grid;
    3x3 tile preview; autosave/recovery.
  - **Layers:** add/delete/reorder, hide/lock, opacity-free (pixel art), merge; .pxd layer chunks.
    Do before the animator.
  - **Camera mode** (filters → pixel art), **text tool**, **left-handed mode**.
  - **v2 animator** after layers.

## Open questions
- `.pxd` details (compression, where saves live on SD) — Claude decides, documents in docs/

## Later
- Layers, export sized for Crow's display, send sprites to a Pi over Wi-Fi
