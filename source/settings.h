// App settings kept between launches: a small "key=value" text file.
// Pure C so it can be unit-tested on a PC.
// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef struct {
	const char* key;
	int* value;
	int min, max;     // loaded values outside this range are ignored
} Setting;

// Writes "key=value\n" lines into out (NUL terminated, cut off at cap). Returns the length.
size_t settings_format(const Setting* items, int n, char* out, size_t cap);
// Reads "key=value" lines. Unknown keys, bad numbers and out-of-range values are skipped, so
// files from older or newer versions are fine.
void settings_parse(const Setting* items, int n, const char* text);
bool settings_load(const char* path, const Setting* items, int n);
bool settings_save(const char* path, const Setting* items, int n);
