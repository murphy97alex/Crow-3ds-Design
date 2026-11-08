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
  mirror drawing (horizontal/vertical symmetry). Eyedropper stays on hold-R.
- **Buttons:** use the draft layout above for v1; left-handed mirror in Settings.
- **Colors:** per-project palette, up to 32 slots. Slots are edited with a hex entry
  and an RGB color wheel. Pixels store a palette index, so editing a slot recolors the art.
  PNG export is full color.
- **Crow display:** unknown for now. Design for full color; display-specific export later.
- **Project file:** custom binary `.pxd`, one file per project: versioned header, canvas size,
  palette, then a list of frames (compressed indexed pixels). Room to add layers later.
  PNG export is separate.

## Roadmap
- **M0 (done, v0.0.1):** scaffold, CI, releases.
- **M1 (done, v0.1.0):** canvas, zoom/pan/grid, palette + hex/wheel picker, eyedropper, undo/redo,
  .pxd save/open, size presets.
- **M2 (done, v0.2.0):** brush sizes 1–9 with Square/Round/Dither shapes (pencil, eraser); pixel-perfect pencil
  (size 1); fill (contiguous or replace color); line, rectangle, ellipse (outline/filled, ZR snap,
  2:1 iso lines); mirror drawing (off / left-right / top-bottom / both); A tool options panel.
- **M3 (v0.3.0):** select + move, copy/paste, flip/rotate 90°; stamps; outline (1px around selection/art);
  shift/wrap canvas; PNG export: one checklist of sizes (1x–8x and 16–256 px) with a
  "Recommended" preset (1x/2x/4x/8x).
- **Later in v1:** HUD layout grid (every 8/16 px) and 3x3 tile preview; shading tool (step
  lighter/darker along a palette ramp); autosave/recovery; text tool; camera mode; left-handed mode.

## Open questions
- Default starting palette (needs an open-licensed choice)
- `.pxd` details (compression, where saves live on SD) — Claude decides, documents in docs/

## Later
- Layers, export sized for Crow's display, send sprites to a Pi over Wi-Fi
