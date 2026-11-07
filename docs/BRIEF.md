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

## Open questions
- v1 tool list, button mapping, palette (fixed vs free), file format details

## Later
- Layers, export sized for Crow's display, send sprites to a Pi over Wi-Fi
