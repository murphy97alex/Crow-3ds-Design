// Host tests: selection/move/transforms, shift, PNG export, stamps.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doc.h"
#include "png.h"
#include "select.h"
#include "stamps.h"

static Doc d;
static uint8_t at(int x, int y) { return doc_get(&d, x, y); }
static int count(uint8_t v) { int n = 0; for (int i = 0; i < d.w * d.h; i++) n += doc_pixels(&d)[i] == v; return n; }

int main(void)
{
	doc_new(&d, 8, 6);
	// 2x1 bar of color 1 at (1,1),(2,1); color 2 at (1,2)
	doc_pixels(&d)[1 * 8 + 1] = 1; doc_pixels(&d)[1 * 8 + 2] = 1; doc_pixels(&d)[2 * 8 + 1] = 2;

	// select + move
	sel_set(&d, 1, 1, 2, 2);
	assert(sel_active() && sel_rect().w == 2 && sel_rect().h == 2);
	sel_lift(&d);
	sel_move_to(&d, 4, 3);
	assert(at(4, 3) == 1 && at(5, 3) == 1 && at(4, 4) == 2 && at(1, 1) == 0);
	sel_commit(&d);
	assert(!sel_floating() && sel_active() && sel_rect().x == 4);
	assert(hist_undo(&d) && at(1, 1) == 1 && at(4, 3) == 0);
	assert(hist_redo(&d) && at(4, 3) == 1);

	// cancel a move restores everything, no undo step
	sel_set(&d, 4, 3, 5, 4); sel_lift(&d); sel_move_to(&d, 0, 0); sel_cancel(&d);
	assert(at(4, 3) == 1 && at(0, 0) == 0 && !sel_active());

	// flip horizontal on plain selection
	sel_set(&d, 4, 3, 5, 4);
	sel_flip(&d, true);
	assert(at(4, 3) == 1 && at(5, 3) == 1 && at(5, 4) == 2 && at(4, 4) == 0 && !sel_floating());
	// rotate 2x2 clockwise: [1 1 / 0 2] -> [0 1 / 2 1]
	sel_rotate(&d);
	assert(at(4, 3) == 0 && at(5, 3) == 1 && at(4, 4) == 2 && at(5, 4) == 1);

	// copy/paste
	sel_copy(&d);
	sel_paste(&d, 0, 0);
	assert(sel_floating() && at(1, 0) == 1 && at(0, 1) == 2);
	sel_commit(&d);
	assert(count(1) == 4 && count(2) == 2 && at(0, 0) == 0 && at(1, 0) == 1 && at(0, 1) == 2);

	// delete
	sel_set(&d, 0, 0, 1, 1); sel_delete(&d);
	assert(count(1) == 2 && count(2) == 1 && !sel_active());

	// outline a single pixel -> plus shape of 4
	doc_new(&d, 7, 7); sel_reset();
	doc_pixels(&d)[3 * 7 + 3] = 1;
	sel_set(&d, 3, 3, 3, 3);
	sel_outline(&d, 5);
	assert(count(5) == 4 && at(3, 2) == 5 && at(2, 3) == 5 && at(2, 2) == 0);

	// rotate non-square selection keeps it centered
	doc_new(&d, 9, 9); sel_reset();
	for (int x = 2; x <= 6; x++) doc_pixels(&d)[4 * 9 + x] = 1; // horizontal 5px bar
	sel_set(&d, 2, 4, 6, 4);
	sel_rotate(&d);
	for (int y = 2; y <= 6; y++) assert(at(4, y) == 1);
	assert(count(1) == 5);

	// shift with wrap
	doc_new(&d, 4, 3); sel_reset();
	doc_pixels(&d)[0] = 1;
	shift_begin(&d); shift_set(&d, -1, 4); shift_end(&d, true);
	assert(at(3, 1) == 1 && count(1) == 1);
	assert(hist_undo(&d) && at(0, 0) == 1);

	// PNG export: 2x2 doc, scaled 3x
	doc_new(&d, 2, 2); sel_reset();
	doc_pixels(&d)[0] = 1; doc_pixels(&d)[3] = 9;
	assert(png_export(&d, 0, 6, 6, "out.png") == NULL);

	// stamps: save with this palette, load into a palette where colors moved
	assert(stamp_save(".", "st", doc_pixels(&d), 2, 2, d.palette) == NULL);
	uint32_t pal2[DOC_PALETTE_SIZE];
	memcpy(pal2, d.palette, sizeof(pal2));
	uint32_t t = pal2[0]; pal2[0] = pal2[20]; pal2[20] = t; // black now at slot 21
	uint8_t* px; int w, h;
	assert(stamp_load(".", "st", pal2, &px, &w, &h) == NULL);
	assert(w == 2 && h == 2 && px[0] == 21 && px[1] == 0 && px[3] == 9);
	free(px);
	char names[8][DOC_NAME_MAX];
	assert(stamp_list(".", names, 8) >= 1);

	sel_reset();
	doc_free(&d);
	hist_clear();
	puts("ok");
	return 0;
}
