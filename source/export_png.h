// PNG export of a frame, scaled with nearest-neighbor (crisp pixels).
// SPDX-License-Identifier: MIT
#pragma once
#include "doc.h"

#define PNG_MAX_DIM 2560

// Returns NULL on success, or a short error message.
const char* png_export(const Doc* d, int frame, int outW, int outH, const char* path);
// Write an RGBA (0xRRGGBBAA) image of sw x sh, scaled to outW x outH.
// Stream rows from a callback (row = outW pixels, 0xRRGGBBAA).
typedef void (*PngxRowFn)(void* ctx, int y, uint32_t* row);
const char* pngx_write_rows(int outW, int outH, PngxRowFn rowFn, void* ctx, const char* path);
const char* pngx_write_rgba(const uint32_t* src, int sw, int sh, int outW, int outH, const char* path);
