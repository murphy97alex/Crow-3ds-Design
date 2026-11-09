# `.pxd` project file format (version 3)

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
| `HEAD` | u16 version (3), u16 width, u16 height, u16 frame count, u16 fps, u16 flags (bit 0 = linked swatches), u16 current layer index, 2 bytes reserved |
| `THMB` | thumbnail of frame 0 for the gallery: u16 w, u16 h (each ≤ 64), then w × h pixels as R,G,B,A bytes. Written right after `HEAD` so it can be read without loading the rest |
| `PALT` | color table: up to 255 colors, u32 each, `0xRRGGBBAA`. Pixel value *n* uses entry *n* (1-based) |
| `SWAT` | the 32 swatches shown in the palette strip, u32 each. Saved colors only; pixels never refer to them |
| `LAYR` | one per layer, bottom first: u16 id, u16 folder id + 1 (0 = none), u8 flags (1 folder, 2 visible, 4 locked, 8 collapsed), u8 opacity, 2 bytes reserved, name (16 bytes, NUL padded). A layer's `FRAM` chunks follow it; folders have none. Folder members sit directly below their folder |
| `FRAM` | one per frame of the preceding layer, in order: u16 duration ms (0 = use fps), u8 encoding (1 = RLE), u8 reserved, pixel data |
| `END ` | empty |

**Pixels** are one byte each, row-major, top-left first: `0` = transparent,
`1..255` = color table entry.

**Version 1/2** files have no `LAYR` chunks: their `FRAM` chunks form a single layer.
**Version 1** files also have no `SWAT` chunk; their 32-color `PALT` is used as both the color
table and the swatches.

**RLE** (encoding 1): pairs of `count` (u8, 1–255) and `value` (u8). The runs must add
up to exactly width × height.

Limits in this version: 320 × 240 max, 64 frames max, 16 layers + folders. Files are written to
`<name>.pxd.tmp` first and then renamed, so a crash mid-save keeps the old file.
