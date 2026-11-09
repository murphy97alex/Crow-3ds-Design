// Renders text with a pixel font into indexed pixels (for the Text tool).
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include "fonts.h"

#define TEXT_MAX 256

// Renders s (\n = new line) with font f, each font pixel scaled to scale x scale, `spacing`
// extra pixels between characters (can be -1), in pixel value fg. If outline != 0, adds a 1px
// outline (around the scaled text) in that value. Returns a malloc'd w*h buffer (0 = clear),
// or NULL for empty text / out of memory.
uint8_t* text_render(const PixFont* f, const char* s, int scale, int spacing, uint8_t fg, uint8_t outline,
                     int* w, int* h);
