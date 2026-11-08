// Stamps: small pieces of art saved to SD and reused across projects.
// SPDX-License-Identifier: MIT
#pragma once
#include "doc.h"

#define STAMP_MAX 64

// Save pixels (with the source palette) as a stamp. NULL on success, else an error.
const char* stamp_save(const char* dir, const char* name, const uint8_t* px, int w, int h,
                       const uint32_t* palette);

// Load a stamp and remap its colors to the nearest colors of `palette`.
// On success returns NULL and *px (malloc'd), *w, *h.
const char* stamp_load(const char* dir, const char* name, const uint32_t* palette,
                       uint8_t** px, int* w, int* h);

// List stamp names (without .pxd), sorted. Returns the count.
int stamp_list(const char* dir, char names[][DOC_NAME_MAX], int max);
