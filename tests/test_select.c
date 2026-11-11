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
	d.colorCount = 10;                     // values 1..10 are real, distinct colors
	for (int i = 1; i <= 10; i++) d.colors[i] = 0x10203000u * i | 0xFF;
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

	// clipboard survives switching documents and keeps exact colors
	{
		Doc a = {0}, b = {0};
		doc_new(&a, 3, 1);
		uint8_t red = doc_color_index(&a, 0xFF0000FF, true);
		a.layers[0].cels[0][1] = red;
		sel_reset();
		sel_set(&a, 0, 0, 2, 0);
		sel_copy(&a);
		sel_clear(&a);
		sel_reset();                       // e.g. opening another project
		doc_new(&b, 4, 1);
		doc_color_index(&b, 0x00FF00FF, true);
		sel_paste(&b, 1, 0);
		sel_commit(&b);
		assert(doc_color(&b, b.layers[0].cels[0][2]) == 0xFF0000FF && b.layers[0].cels[0][1] == 0);
		sel_reset();
		doc_free(&a);
		doc_free(&b);
	}

	// magic wand: connected vs all of color; moving keeps the shape
	doc_new(&d, 5, 1); sel_reset();
	d.colorCount = 4;
	for (int i = 1; i <= 4; i++) d.colors[i] = 0x11223300u * i | 0xFF;
	doc_pixels(&d)[0] = 4; doc_pixels(&d)[1] = 4; doc_pixels(&d)[3] = 4;
	sel_wand(&d, 0, 0, false, 0, false, SELOP_NEW);
	assert(sel_active() && sel_has_mask() == false && sel_rect().w == 2);   // full 2x1 rect
	sel_wand(&d, 0, 0, true, 0, false, SELOP_NEW);
	assert(sel_rect().x == 0 && sel_rect().w == 4 && sel_has_mask() && !sel_contains(2, 0) && sel_contains(3, 0));
	sel_lift(&d);
	sel_move_to(&d, 1, 0);
	assert(at(0, 0) == 0 && at(1, 0) == 4 && at(2, 0) == 4 && at(4, 0) == 4 && at(3, 0) == 0);
	sel_commit(&d);
	assert(sel_has_mask() && sel_contains(4, 0) && !sel_contains(3, 0));
	sel_clear(&d);

	// wand tolerance, sampling all layers, add / subtract, invert
	doc_new(&d, 6, 1); sel_reset();
	{
		uint8_t a = doc_color_index(&d, 0x808080FF, true), b = doc_color_index(&d, 0x848484FF, true), c = doc_color_index(&d, 0xFF0000FF, true);
		uint8_t* px = doc_pixels(&d);
		px[0] = a; px[1] = b; px[2] = c; px[3] = a; px[4] = 0; px[5] = 0;
		sel_wand(&d, 0, 0, false, 0, false, SELOP_NEW);
		assert(sel_contains(0, 0) && !sel_contains(1, 0));               // exact only
		sel_wand(&d, 0, 0, false, 5, false, SELOP_NEW);
		assert(sel_contains(1, 0) && !sel_contains(2, 0) && !sel_contains(3, 0)); // 5% joins the near-gray
		sel_wand(&d, 3, 0, false, 0, false, SELOP_ADD);
		assert(sel_contains(0, 0) && sel_contains(1, 0) && sel_contains(3, 0) && !sel_contains(2, 0));
		sel_wand(&d, 1, 0, false, 0, false, SELOP_SUBTRACT);
		assert(sel_contains(0, 0) && !sel_contains(1, 0) && sel_contains(3, 0));
		sel_invert(&d);
		assert(!sel_contains(0, 0) && sel_contains(1, 0) && sel_contains(2, 0) && !sel_contains(3, 0) && sel_contains(5, 0));
		sel_rect_op(&d, 4, 0, 5, 0, SELOP_SUBTRACT);
		assert(!sel_contains(4, 0) && sel_contains(2, 0));
		// sample all layers: art on another layer, wand from an empty layer
		sel_clear(&d);
		doc_layer_add(&d, false);                                           // empty layer on top, current
		sel_wand(&d, 2, 0, false, 0, true, SELOP_NEW);
		assert(sel_contains(2, 0) && !sel_contains(0, 0) && !sel_contains(4, 0));
		sel_wand(&d, 2, 0, false, 0, false, SELOP_NEW);                     // current layer only: all empty
		assert(sel_contains(0, 0) && sel_contains(5, 0));
		sel_clear(&d);
	}

	// lasso: triangle
	doc_new(&d, 10, 10); sel_reset();
	{
		int tri[] = { 0, 0, 9, 0, 0, 9 };
		sel_lasso(&d, tri, 3, SELOP_NEW);
		assert(sel_active() && sel_contains(1, 1) && sel_contains(9, 0) && !sel_contains(9, 9) && !sel_contains(8, 8));
		for (int i = 0; i < 100; i++) doc_pixels(&d)[i] = 1;
		uint8_t* p; int w, h;
		assert(sel_get_pixels(&d, &p, &w, &h) && w == 10 && h == 10 && p[0] == 1 && p[99] == 0);
		free(p);
		sel_delete(&d);
		assert(at(1, 1) == 0 && at(9, 9) == 1);
	}

	// transform: scale from the source, rotate, swap source keeps scale
	doc_new(&d, 40, 40); sel_reset();
	{
		uint8_t img[6] = { 1, 2, 3, 4, 5, 6 };            // 3 x 2
		sel_float_pixels(&d, img, 3, 2, 10, 10);
		sel_transform(&d, 10, 10, 6, 4, 0, false);       // 2x
		Rect r = sel_rect();
		assert(r.x == 10 && r.y == 10 && r.w == 6 && r.h == 4);
		assert(at(10, 10) == 1 && at(11, 11) == 1 && at(12, 10) == 2 && at(15, 13) == 6);
		sel_transform(&d, 10, 10, 3, 2, 0, false);       // back to 1x: exact original, no loss
		assert(at(10, 10) == 1 && at(12, 11) == 6 && at(13, 10) == 0);
		sel_transform(&d, 10, 10, 3, 2, 90, true);       // 90 degrees around the centre
		r = sel_rect();
		assert(r.w == 2 && r.h == 3);
		int n = 0; for (int k = 1; k <= 6; k++) n += count(k);
		assert(n == 6);
		sel_transform(&d, 10, 10, 6, 4, 45, true);        // any angle: bigger box, pixels kept
		r = sel_rect();
		assert(r.w > 6 && r.h > 4);
		float x, y, w, h, a;
		sel_transform_get(&x, &y, &w, &h, &a);
		assert(w == 6 && h == 4 && a == 45);
		uint8_t img2[2] = { 7, 7 };                      // retype: new 2 x 1 source, same 2x scale
		sel_transform(&d, 10, 10, 6, 4, 0, false);
		sel_set_source(&d, img2, 2, 1);
		r = sel_rect();
		assert(r.w == 4 && r.h == 2 && count(7) == 8);
		sel_move_to(&d, 0, 0);
		sel_transform_get(&x, &y, &w, &h, &a);
		assert(x == 0 && y == 0);
		sel_commit(&d);
		assert(count(7) == 8);
		sel_clear(&d);
	}

	// shift with wrap
	doc_new(&d, 4, 3); sel_reset();
	doc_pixels(&d)[0] = doc_color_index(&d, 0x445566FF, true);   // value 1
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

	// floating pixels with a mirror axis moved off centre: copies that land outside are skipped
	sel_reset();
	assert(doc_new(&d, 32, 32));
	{
		uint8_t dot[4] = { 1, 1, 1, 1 };
		d.colors[1] = 0xFF0000FF; d.colorCount = 1;
		sel_float_pixels(&d, dot, 2, 2, 10, 28);
		sel_set_float_mirror(MIRROR_XY);
		sel_set_mirror_axis(0, 4);            // top-bottom axis at y = 2: the copy would be at y = -25
		sel_move_to(&d, 10, 29);
		sel_set_mirror_axis(0, 62);           // axis at y = 31: copy at y = 31..32, half outside
		sel_move_to(&d, 10, 29);
		sel_commit(&d);
		assert(count(1) == 12);               // 2x2, its left-right copy, and two 2x1 copies across y = 31
		sel_set_mirror_axis(0, 0);
	}

	// paste into a nearly full color table: every pasted color survives the compaction
	sel_reset();
	assert(doc_new(&d, 32, 32));
	{
		for (int i = 0; i < 250; i++) doc_pixels(&d)[i] = doc_color_index(&d, 0x000000FF | ((uint32_t)(i + 1) << 16), false);
		for (int i = 0; i < 100; i++) doc_color_index(&d, 0x00FF00FF | ((uint32_t)(i + 1) << 24), true);   // unused entries
		static Doc s2;
		assert(doc_new(&s2, 5, 1));
		for (int i = 0; i < 5; i++) doc_pixels(&s2)[i] = doc_color_index(&s2, 0xFF0000FF | ((uint32_t)(i * 20) << 8), false);
		sel_all(&s2); sel_copy(&s2); sel_reset();
		sel_paste(&d, 0, 20); sel_commit(&d);
		for (int i = 0; i < 5; i++) assert(doc_color(&d, at(i, 20)) == (0xFF0000FF | ((uint32_t)(i * 20) << 8)));
		for (int i = 0; i < 250; i++) assert(doc_color(&d, doc_pixels(&d)[i]) == (0x000000FF | ((uint32_t)(i + 1) << 16)));
		doc_free(&s2);
	}

	sel_reset();
	doc_free(&d);
	hist_clear();
	puts("ok");
	return 0;
}
