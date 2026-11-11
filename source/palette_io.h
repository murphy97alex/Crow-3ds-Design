// Palette files: .hex (one RRGGBB per line, as on Lospec) and .gpl (GIMP / Aseprite / Krita).
// Pure C so it can be unit-tested on a PC.
// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

// Colors are 0xRRGGBBAA. Each returns NULL on success, or a short error message.
const char* palette_save_hex(const char* path, const uint32_t* colors, int n);
const char* palette_save_gpl(const char* path, const char* name, const uint32_t* colors, int n);
// Reads either format (told apart by content). Returns the number of colors read (0 = none /
// not a palette file); at most `max`.
int palette_load(const char* path, uint32_t* out, int max);
int palette_parse(const char* text, uint32_t* out, int max);
