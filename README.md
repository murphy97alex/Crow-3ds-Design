# Pixel UI Designer (3DS)

Homebrew pixel-art / UI drawing app for the New 3DS, written in C with
libctru + citro2d. See [docs/BRIEF.md](docs/BRIEF.md) for the plan.

**Milestone 0:** top screen shows the app name, bottom screen lets you draw
black pixels with the stylus. START exits.

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
