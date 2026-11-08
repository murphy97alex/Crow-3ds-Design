// Host-side tests for drawing tools.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "doc.h"
#include "tools.h"

static Doc d;
static int count(uint8_t v) { int n = 0; for (int i = 0; i < d.w * d.h; i++) n += doc_pixels(&d)[i] == v; return n; }
static uint8_t at(int x, int y) { return doc_get(&d, x, y); }
int main(void)
{
	Brush b1 = { 1, BRUSH_SQUARE, false };
	Brush pp = { 1, BRUSH_SQUARE, true };

	// pixel-perfect removes L corners: a staircase drawn as right,down,right,down
	doc_new(&d, 8, 8);
	tools_begin(&d, MIRROR_OFF);
	tools_stroke_start(0, 0, 1, &pp);
	tools_stroke_to(1, 0, 1, &pp); tools_stroke_to(1, 1, 1, &pp);
	tools_stroke_to(2, 1, 1, &pp); tools_stroke_to(2, 2, 1, &pp);
	assert(at(0, 0) && at(1, 1) && at(2, 2) && !at(1, 0) && !at(2, 1));
	assert(count(1) == 3);

	// without pixel-perfect the corners stay
	doc_new(&d, 8, 8);
	tools_begin(&d, MIRROR_OFF);
	tools_stroke_start(0, 0, 1, &b1);
	tools_stroke_to(1, 0, 1, &b1); tools_stroke_to(1, 1, 1, &b1);
	assert(count(1) == 3 && at(1, 0));

	// brush sizes
	doc_new(&d, 16, 16);
	tools_begin(&d, MIRROR_OFF);
	Brush sq3 = { 3, BRUSH_SQUARE, false }, rd3 = { 3, BRUSH_ROUND, false }, rd5 = { 5, BRUSH_ROUND, false };
	tools_stroke_start(4, 4, 1, &sq3); assert(count(1) == 9);
	tools_stroke_start(10, 4, 2, &rd3); assert(count(2) == 5);
	tools_stroke_start(8, 10, 3, &rd5); assert(count(3) == 21);
	Brush dz = { 4, BRUSH_DITHER, false };
	tools_stroke_start(1, 12, 4, &dz); assert(count(4) == 8);

	// mirror both
	doc_new(&d, 10, 6);
	tools_begin(&d, MIRROR_XY);
	tools_stroke_start(1, 1, 1, &b1);
	assert(at(1, 1) && at(8, 1) && at(1, 4) && at(8, 4) && count(1) == 4);

	// rect outline / filled
	doc_new(&d, 10, 10);
	tools_begin(&d, MIRROR_OFF);
	tools_rect(7, 7, 2, 2, 1, 1, false); assert(count(1) == 20);
	tools_restore(); assert(count(1) == 0 && !tools_changed());
	tools_rect(2, 2, 7, 7, 1, 2, false); assert(count(1) == 36 - 4);
	tools_rect(0, 0, 9, 9, 2, 1, true); assert(count(2) == 100);

	// ellipse: filled is symmetric, outline is thin and inside the box
	doc_new(&d, 21, 15);
	tools_begin(&d, MIRROR_OFF);
	tools_ellipse(0, 0, 20, 14, 1, 1, false);
	for (int y = 0; y < 15; y++) for (int x = 0; x < 21; x++) assert(at(x, y) == at(20 - x, y) && at(x, y) == at(x, 14 - y));
	assert(at(10, 0) && at(0, 7) && !at(10, 7));
	int outline = count(1);
	tools_ellipse(0, 0, 20, 14, 2, 1, true);
	assert(count(2) > outline);

	// fill contiguous vs replace
	doc_new(&d, 10, 10);
	tools_begin(&d, MIRROR_OFF);
	tools_rect(0, 0, 4, 4, 1, 1, false);
	tools_stroke_start(8, 8, 1, &b1);
	tools_fill(2, 2, 2, false); assert(count(2) == 9);
	tools_fill(0, 0, 3, true); assert(count(3) == 17 && count(1) == 0);
	tools_fill(9, 0, 4, false); assert(count(4) == 100 - 9 - 17);

	// snapping
	int x = 9, y = 4; tools_snap_line(0, 0, &x, &y); assert(x == 8 && y == 4);
	x = -5; y = 6; tools_snap_line(0, 0, &x, &y); assert(x == -6 && y == 6);
	x = 7; y = 1; tools_snap_line(0, 0, &x, &y); assert(x == 7 && y == 0);
	x = 3; y = -8; tools_snap_square(0, 0, &x, &y); assert(x == 8 && y == -8);

	// line with brush
	doc_new(&d, 10, 10);
	tools_begin(&d, MIRROR_OFF);
	tools_line(0, 5, 9, 5, 1, &b1); assert(count(1) == 10);

	doc_free(&d);
	hist_clear();
	puts("ok");
	return 0;
}
