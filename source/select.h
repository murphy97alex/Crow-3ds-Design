// Selection, floating pixels (move/paste/stamps), clipboard, transforms, canvas shift.
// Pure C so it can be unit-tested on a PC. Undo is handled here via hist_begin/commit.
// SPDX-License-Identifier: MIT
#pragma once
#include "doc.h"

typedef struct { int x, y, w, h; } Rect;

bool sel_active(void);
bool sel_floating(void);
Rect sel_rect(void);                 // selection or floating rect, canvas coords
bool sel_contains(int x, int y);
bool sel_has_mask(void);             // false = the whole rectangle is selected
bool sel_mask_at(int i, int j);      // inside the selection, relative to sel_rect()

// Shaped selections on the current layer (replace the current selection).
void sel_wand(Doc* d, int x, int y, bool allOfColor);  // same color, connected or everywhere
void sel_lasso(Doc* d, const int* pts, int n);         // closed polygon, pts = x0,y0,x1,y1...

void sel_set(Doc* d, int x0, int y0, int x1, int y1); // corners, inclusive; clamped
void sel_all(Doc* d);
void sel_clear(Doc* d);              // deselect (drops floating pixels in place)
void sel_reset(void);                // forget everything (document replaced)

// Floating pixels: lifted from the canvas (move) or pasted/stamped.
void sel_lift(Doc* d);               // cut the selection into a floating layer
void sel_move_to(Doc* d, int x, int y);
void sel_commit(Doc* d);             // drop floating pixels (one undo step); keeps selection rect
void sel_cancel(Doc* d);             // undo the lift/paste entirely
void sel_float_pixels(Doc* d, const uint8_t* px, int w, int h, int x, int y);
// Also draw mirrored copies of the floating pixels (used for stamps); reset on commit/cancel.
#include "tools.h"
void sel_set_float_mirror(Mirror m);
// Replace the floating pixels (e.g. a stamp being resized) without a new undo step.
void sel_float_replace(Doc* d, const uint8_t* px, int w, int h, int x, int y);

// Actions (apply to the floating layer, or lift+apply+commit for a plain selection)
void sel_copy(Doc* d);
bool sel_has_clip(void);
void sel_cut(Doc* d);
void sel_paste(Doc* d, int x, int y);
void sel_delete(Doc* d);
void sel_flip(Doc* d, bool horizontal);
void sel_rotate(Doc* d);             // 90° clockwise
void sel_outline(Doc* d, uint8_t v); // 1px outline around non-transparent pixels

// Current selection/floating pixels as a standalone buffer (for stamps). Returns false if none.
bool sel_get_pixels(Doc* d, uint8_t** px, int* w, int* h);

// Shift whole frame with wrap-around: begin, set total offset (repeatable), end.
void shift_begin(Doc* d);
void shift_set(Doc* d, int dx, int dy);
void shift_end(Doc* d, bool keep);
