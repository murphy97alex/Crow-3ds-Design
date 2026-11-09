// Raster drawing operations on the current frame: brushes, shapes, fill, mirror.
// Pure C (no 3DS deps) so it can be unit-tested on a PC.
// SPDX-License-Identifier: MIT
#pragma once
#include "doc.h"

typedef enum { BRUSH_SQUARE, BRUSH_ROUND, BRUSH_DITHER, BRUSH_SHAPE_COUNT } BrushShape;
typedef enum { MIRROR_OFF, MIRROR_X, MIRROR_Y, MIRROR_XY, MIRROR_COUNT } Mirror;

#define BRUSH_MAX 9

typedef struct {
	int size;           // 1..BRUSH_MAX
	BrushShape shape;
	bool pixelPerfect;  // only used at size 1 with square/round
} Brush;

// Mirror axis as twice the centre coordinate (so it can sit between pixels); 0 = canvas centre.
void tools_set_mirror_axis(int ax2, int ay2);

// Start an operation: remembers the frame as it is now (for pixel-perfect and shape previews).
bool tools_begin(Doc* d, Mirror mirror);
void tools_restore(void);   // put the frame back to how it was at tools_begin
bool tools_changed(void);   // did anything change since tools_begin?

// Freehand strokes
void tools_stroke_start(int x, int y, uint8_t v, const Brush* b);
void tools_stroke_to(int x, int y, uint8_t v, const Brush* b);

// Shapes (corner to corner, inclusive)
void tools_line(int x0, int y0, int x1, int y1, uint8_t v, const Brush* b);
void tools_rect(int x0, int y0, int x1, int y1, uint8_t v, int size, bool filled);
void tools_ellipse(int x0, int y0, int x1, int y1, uint8_t v, int size, bool filled);
void tools_fill(int x, int y, uint8_t v, bool replaceAll);
// Dithered gradient (4x4 Bayer) inside the connected area at (x0,y0): color v at the start,
// fading into the area's own color toward (x1,y1).
void tools_gradient_fill(int x0, int y0, int x1, int y1, uint8_t v);

// While set, drawing ignores the value passed in and writes map[original pixel] instead
// (used by the shading tool). Pass NULL to turn off.
void tools_set_value_map(const uint8_t* map);

// While set, drawing only changes pixels for which inside(x, y) is true (the selection).
void tools_set_clip(bool (*inside)(int x, int y));

// Snapping helpers (adjust the end point)
void tools_snap_line(int x0, int y0, int* x1, int* y1);   // 0/45/90° and 2:1 slopes
void tools_snap_square(int x0, int y0, int* x1, int* y1);
