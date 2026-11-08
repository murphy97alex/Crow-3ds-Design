// Host tests: selection/move/transforms, shift, PNG export, stamps.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doc.h"
#include "export_png.h"
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

	// replacing floating pixels (stamp resize) keeps one undo step
	doc_new(&d, 6, 6); sel_reset();
	{
		uint8_t a[1] = { 3 }, b[4] = { 4, 4, 4, 4 };
		sel_float_pixels(&d, a, 1, 1, 0, 0);
		sel_float_replace(&d, b, 2, 2, 1, 1);
		assert(at(0, 0) == 0 && at(1, 1) == 4 && at(2, 2) == 4 && count(4) == 4);
		sel_commit(&d);
		assert(hist_undo(&d) && count(4) == 0 && !hist_undo(&d));
	}

	// mirrored stamp: copies appear while floating and stay after commit
	doc_new(&d, 6, 4); sel_reset();
	{
		uint8_t a[1] = { 2 };
		sel_float_pixels(&d, a, 1, 1, 0, 1);
		sel_set_float_mirror(MIRROR_XY);
		sel_move_to(&d, 1, 0);
		assert(at(1, 0) == 2 && at(4, 0) == 2 && at(1, 3) == 2 && at(4, 3) == 2 && count(2) == 4);
		sel_commit(&d);
		assert(count(2) == 4);
		uint8_t b[1] = { 3 };
		sel_float_pixels(&d, b, 1, 1, 0, 0);   // mirror resets after commit
		assert(count(3) == 1);
		sel_cancel(&d);
	}

	// magic wand: connected vs all of color; moving keeps the shape
	doc_new(&d, 5, 1); sel_reset();
	doc_pixels(&d)[0] = 4; doc_pixels(&d)[1] = 4; doc_pixels(&d)[3] = 4;
	sel_wand(&d, 0, 0, false);
	assert(sel_active() && sel_has_mask() == false && sel_rect().w == 2);   // full 2x1 rect
	sel_wand(&d, 0, 0, true);
	assert(sel_rect().x == 0 && sel_rect().w == 4 && sel_has_mask() && !sel_contains(2, 0) && sel_contains(3, 0));
	sel_lift(&d);
	sel_move_to(&d, 1, 0);
	assert(at(0, 0) == 0 && at(1, 0) == 4 && at(2, 0) == 4 && at(4, 0) == 4 && at(3, 0) == 0);
	sel_commit(&d);
	assert(sel_has_mask() && sel_contains(4, 0) && !sel_contains(3, 0));
	sel_clear(&d);

	// lasso: triangle
	doc_new(&d, 10, 10); sel_reset();
	{
		int tri[] = { 0, 0, 9, 0, 0, 9 };
		sel_lasso(&d, tri, 3);
		assert(sel_active() && sel_contains(1, 1) && sel_contains(9, 0) && !sel_contains(9, 9) && !sel_contains(8, 8));
		for (int i = 0; i < 100; i++) doc_pixels(&d)[i] = 1;
		uint8_t* p; int w, h;
		assert(sel_get_pixels(&d, &p, &w, &h) && w == 10 && h == 10 && p[0] == 1 && p[99] == 0);
		free(p);
		sel_delete(&d);
		assert(at(1, 1) == 0 && at(9, 9) == 1);
	}

	// shift with wrap
	doc_new(&d, 4, 3); sel_reset();
	doc_pixels(&d)[0] = 1;
	shift_begin(&d); shift_set(&d, -1, 4); shift_end(&d, true);
	assert(at(3, 1) == 1 && count(1) == 1);
	assert(hist_undo(&d) && at(0, 0) == 1);

	// PNG export: 2x2 doc, scaled 3x
	doc_new(&d, 2, 2); sel_reset();
	doc_pixels(&d)[0] = doc_color_index(&d, 0x000000FF, true);
	doc_pixels(&d)[3] = doc_color_index(&d, 0xE8463CFF, true);
	assert(png_export(&d, 0, 6, 6, "out.png") == NULL);

	// stamps: colors carry over exactly into another document's color table
	assert(stamp_save(".", "st", doc_pixels(&d), 2, 2, &d) == NULL);
	Doc o = {0}; doc_new(&o, 4, 4);
	doc_color_index(&o, 0x123456FF, true);              // o already has one color
	uint8_t* px; int w, h;
	assert(stamp_load(".", "st", &o, &px, &w, &h) == NULL);
	assert(w == 2 && h == 2 && px[1] == 0 && px[2] == 0);
	assert(doc_color(&o, px[0]) == 0x000000FF && doc_color(&o, px[3]) == 0xE8463CFF && o.colorCount == 3);
	free(px);
	doc_free(&o);
	char names[8][DOC_NAME_MAX];
	assert(stamp_list(".", names, 8) >= 1);

	sel_reset();
	doc_free(&d);
	hist_clear();
	puts("ok");
	return 0;
}
