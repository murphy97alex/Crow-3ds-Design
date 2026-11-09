// Animation playback order and exports: sprite sheets, numbered PNG frames, animated GIF.
// SPDX-License-Identifier: MIT
#pragma once
#include "doc.h"

#define ANIM_MAX_SEQ (DOC_MAX_FRAMES * 2)
#define GIF_MAX_DIM 1280

typedef enum { SHEET_ROW, SHEET_COLUMN, SHEET_GRID, SHEET_LAYOUT_COUNT } SheetLayout;

// Frame range of a tag (tag < 0 or out of range = all frames).
void anim_range(const Doc* d, int tag, int* from, int* to);
// Playback order of one loop: forward, reverse or ping-pong (ends not repeated).
// Fills out[ANIM_MAX_SEQ]; returns the count.
int anim_sequence(const Doc* d, int tag, int* out);
// Columns x rows of a sheet with n frames.
void anim_sheet_grid(int n, SheetLayout layout, int* cols, int* rows);

// Each returns NULL on success, or a short error message.
// Sprite sheet of the frames in the tag's range (in order), each scaled by `scale`.
const char* anim_export_sheet(const Doc* d, int tag, SheetLayout layout, int scale, const char* path);
// Numbered PNGs: `base` + "_001.png", "_002.png", ...
const char* anim_export_frames(const Doc* d, int tag, int scale, const char* base);
// Looping animated GIF in playback order with each frame's duration.
const char* gif_export(const Doc* d, int tag, int scale, const char* path);
