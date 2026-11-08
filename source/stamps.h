// Stamps: small pieces of art saved to SD and reused across projects.
// SPDX-License-Identifier: MIT
#pragma once
#include "doc.h"

#define STAMP_MAX 64

// Save pixels (values from src's color table) as a stamp. NULL on success, else an error.
const char* stamp_save(const char* dir, const char* name, const uint8_t* px, int w, int h,
                       const Doc* src);

// Load a stamp, adding its colors to target's color table (exact colors; may compact the
// table, so call it when nothing else holds pixel values). On success returns NULL and
// *px (malloc'd, values for target), *w, *h.
const char* stamp_load(const char* dir, const char* name, Doc* target,
                       uint8_t** px, int* w, int* h);

// List stamp names (without .pxd), sorted. Returns the count.
int stamp_list(const char* dir, char names[][DOC_NAME_MAX], int max);
