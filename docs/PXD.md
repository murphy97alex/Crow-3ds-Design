# `.pxd` project file format (version 2)

One file per project, saved to `sdmc:/3ds/pixel-ui-designer/projects/<name>.pxd`.
All integers are little-endian.

```
magic   4 bytes  "PXD" 0x1A
chunks  repeated: id (4 ASCII bytes), length (u32), payload (length bytes)
```

Readers skip chunks they don't know, so new data (layers, stamps, …) can be added
without breaking old files. A file ends with an `END ` chunk.

| Chunk | Payload |
|-------|---------|
| `HEAD` | u16 version (1), u16 width, u16 height, u16 frame count, u16 fps, u16 flags (0), 4 bytes reserved |
| `PALT` | color table: up to 255 colors, u32 each, `0xRRGGBBAA`. Pixel value *n* uses entry *n* (1-based) |
| `SWAT` | the 32 swatches shown in the palette strip, u32 each. Saved colors only; pixels never refer to them |
| `FRAM` | one per frame, in order: u16 duration ms (0 = use fps), u8 encoding (1 = RLE), u8 reserved, pixel data |
| `END ` | empty |

**Pixels** are one byte each, row-major, top-left first: `0` = transparent,
`1..255` = color table entry.

**Version 1** files have no `SWAT` chunk; their 32-color `PALT` is used as both the color
table and the swatches.

**RLE** (encoding 1): pairs of `count` (u8, 1–255) and `value` (u8). The runs must add
up to exactly width × height.

Limits in this version: 320 × 240 max, 64 frames max. Files are written to
`<name>.pxd.tmp` first and then renamed, so a crash mid-save keeps the old file.
