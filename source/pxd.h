// .pxd project files. Format: docs/PXD.md
// SPDX-License-Identifier: MIT
#pragma once
#include "doc.h"

#define PXD_VERSION 4

// Returns NULL on success, or a short error message.
const char* pxd_save(const Doc* d, const char* path);
const char* pxd_load(Doc* d, const char* path); // does not touch undo history

#define PXD_THUMB 64   // thumbnails fit in 64x64

// Thumbnail of frame 0 (all visible layers), fitted into PXD_THUMB x PXD_THUMB with crisp
// pixels: small canvases are enlarged by a whole number, big ones shrunk. out = RGBA.
void pxd_make_thumb(const Doc* d, uint32_t* out, int* tw, int* th);

// Reads only the header and stored thumbnail of a project (fast, for the gallery).
// Returns false if the file can't be read. *hasThumb is false for files saved before
// thumbnails existed (load the whole project and use pxd_make_thumb instead).
bool pxd_read_info(const char* path, int* w, int* h, int* layers, int* frames, bool* hasThumb,
                   uint32_t* thumb, int* tw, int* th);
