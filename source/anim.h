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

// Sprite sheet options. pad = transparent pixels between cells (in the exported image).
// trim = cut the empty border away; every frame is cut by the same box (the smallest that
// holds all of them), so cells stay one size.
typedef struct { SheetLayout layout; int scale, pad; bool trim; } SheetOpts;
// Size of the sheet these options give; false if it is too big to export.
bool anim_sheet_size(const Doc* d, int tag, const SheetOpts* o, int* w, int* h);

// Each returns NULL on success, or a short error message.
// Sprite sheet of the frames in the tag's range (in order).
const char* anim_export_sheet(const Doc* d, int tag, const SheetOpts* o, const char* path);
// Data file for the sheet in Aseprite's JSON "array" layout (read by Godot, Unity, Phaser...):
// frame rectangles, durations, tags in the range. `image` = the sheet's file name.
const char* anim_export_sheet_json(const Doc* d, int tag, const SheetOpts* o, const char* image, const char* path);
// Numbered PNGs: `base` + "_001.png", "_002.png", ...
const char* anim_export_frames(const Doc* d, int tag, int scale, const char* base);
// Looping animated GIF in playback order with each frame's duration.
const char* gif_export(const Doc* d, int tag, int scale, const char* path);
