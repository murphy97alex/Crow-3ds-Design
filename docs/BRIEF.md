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

## Button layout (draft, not locked)
Stylus in right hand, frequent actions on left.
- Touch: draw | L: undo | ZL: redo
- D-pad L/R: prev/next tool | Up/Down: zoom
- Circle pad: pan | R (hold): eyedropper
- A: confirm/place stamp | B: cancel/deselect
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

## Open questions
- Default starting palette (needs an open-licensed choice)
- `.pxd` details (compression, where saves live on SD) — Claude decides, documents in docs/

## Later
- Layers, export sized for Crow's display, send sprites to a Pi over Wi-Fi
