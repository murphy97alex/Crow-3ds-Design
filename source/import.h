// PNG import: read a PNG and turn it into indexed pixels for a document.
// SPDX-License-Identifier: MIT
#pragma once
#include "doc.h"

#define IMPORT_MAX_DIM 16384

// Reads a PNG as straight RGBA (0xRRGGBBAA per pixel), shrunk with nearest-neighbor while
// decoding so it fits in maxW x maxH (never enlarged). Reads one row at a time, so even very
// large images need little memory. *shrunk tells whether it was made smaller.
// NULL on success, else an error.
const char* import_read_png(const char* path, int maxW, int maxH, uint32_t** rgba, int* w, int* h, bool* shrunk);

// Nearest-neighbor shrink so the image fits in maxW x maxH (keeps proportions). Never enlarges.
// Replaces *rgba/*w/*h. Returns false on out of memory.
bool import_fit(uint32_t** rgba, int* w, int* h, int maxW, int maxH);

// Converts RGBA to pixel values in d's color table (alpha < 128 = transparent). If the image
// has more colors than fit, the nearest existing colors are used. May compact d's color table.
uint8_t* import_to_indexed(Doc* d, const uint32_t* rgba, int w, int h);

// Up to `max` distinct opaque colors, most common first. Returns the count.
int import_palette(const uint32_t* rgba, int w, int h, uint32_t* out, int max);

// ---- animations ------------------------------------------------------------------
// Both replace d with a new multi-frame project on success and leave it alone on failure.
// The undo history is cleared either way. Colors beyond the 255 a project can hold use the nearest one.

// Sprite sheet: the image is cut into cols x rows equal cells, read left to right, top to
// bottom. Empty cells at the end are dropped. Cells must fit a canvas (320 x 240).
const char* import_sheet_doc(Doc* d, const uint32_t* rgba, int w, int h, int cols, int rows);
// Animated (or still) GIF, with each frame's duration. Bigger than 320 x 240 is shrunk to fit.
// *truncated is set when the GIF has more frames than a project can hold. The file is
// treated as untrusted.
const char* import_gif_doc(Doc* d, const char* path, bool* truncated);
const char* import_gif_mem(Doc* d, const uint8_t* data, size_t size, bool* truncated);
