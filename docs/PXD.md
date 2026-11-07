# `.pxd` project file format (version 1)

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
| `PALT` | up to 32 colors, u32 each, `0xRRGGBBAA` |
| `FRAM` | one per frame, in order: u16 duration ms (0 = use fps), u8 encoding (1 = RLE), u8 reserved, pixel data |
| `END ` | empty |

**Pixels** are one byte each, row-major, top-left first: `0` = transparent,
`1..32` = palette slot 1..32.

**RLE** (encoding 1): pairs of `count` (u8, 1–255) and `value` (u8). The runs must add
up to exactly width × height.

Limits in this version: 320 × 240 max, 64 frames max. Files are written to
`<name>.pxd.tmp` first and then renamed, so a crash mid-save keeps the old file.
