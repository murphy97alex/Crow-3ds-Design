// Host-side test for .pxd save/load and undo history: cc -I source tests/test_pxd.c source/pxd.c source/doc.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doc.h"
#include "pxd.h"

int main(void)
{
	Doc a = {0}, b = {0};
	assert(doc_new(&a, 48, 30));
	for (int i = 0; i < 48 * 30; i++) a.frames[0][i] = (i * 7 / 13) % 33;
	a.colorCount = 32;
	for (int i = 1; i <= 32; i++) a.colors[i] = 0x01020300u * i | 0xFF;
	a.swatches[3] = 0x12345678;
	a.frames[1] = calloc(48 * 30, 1); a.frames[1][5] = 9; a.frameCount = 2;
	assert(pxd_save(&a, "test.pxd") == NULL);
	assert(pxd_load(&b, "test.pxd") == NULL);
	assert(b.w == 48 && b.h == 30 && b.frameCount == 2 && b.swatches[3] == 0x12345678 && b.colorCount == 32 && b.colors[7] == a.colors[7]);
	assert(!memcmp(a.frames[0], b.frames[0], 48 * 30) && !memcmp(a.frames[1], b.frames[1], 48 * 30));

	// color table: find/add, compaction when full, nearest fallback
	Doc e = {0}; doc_new(&e, 4, 1);
	assert(e.colorCount == 0);
	uint8_t red = doc_color_index(&e, 0xFF0000FF, true);
	assert(red == 1 && doc_color_index(&e, 0xFF0000FF, true) == 1 && e.colorCount == 1);
	for (int i = 0; i < 300; i++) e.frames[0][i % 4] = doc_color_index(&e, 0x00000AFF + ((uint32_t)i << 8), true);
	assert(e.colorCount <= DOC_MAX_COLORS);
	for (int x = 0; x < 4; x++) assert(doc_color(&e, e.frames[0][x]) == 0x00000AFF + ((uint32_t)(296 + x) << 8));
	doc_free(&e);

	// recolor: swap two colors at once, then merge into an existing color
	{
		Doc r = {0}; doc_new(&r, 3, 1);
		uint8_t A = doc_color_index(&r, 0xFF0000FF, true), B = doc_color_index(&r, 0x0000FFFF, true);
		r.frames[0][0] = A; r.frames[0][1] = B; r.frames[0][2] = 0;
		uint32_t from[2] = { 0xFF0000FF, 0x0000FFFF }, to[2] = { 0x0000FFFF, 0xFF0000FF };
		assert(doc_recolor(&r, from, to, 2));
		assert(doc_color(&r, r.frames[0][0]) == 0x0000FFFF && doc_color(&r, r.frames[0][1]) == 0xFF0000FF);
		uint32_t f2 = 0x0000FFFF, t2 = 0xFF0000FF;   // now both pixels red -> same entry
		assert(doc_recolor(&r, &f2, &t2, 1));
		assert(r.frames[0][0] == r.frames[0][1] && doc_color(&r, r.frames[0][0]) == 0xFF0000FF);
		uint32_t f3 = 0x00FF00FF, t3 = 0x123456FF;   // color not in drawing
		assert(!doc_recolor(&r, &f3, &t3, 1));
		r.linkedSwatches = true;
		assert(pxd_save(&r, "lk.pxd") == NULL);
		Doc q = {0}; assert(pxd_load(&q, "lk.pxd") == NULL && q.linkedSwatches);
		doc_free(&q); doc_free(&r);
	}

	// version 1 file: 32-entry palette is both color table and swatches
	{
		FILE* f = fopen("v1.pxd", "wb");
		uint8_t head[16] = { 1, 0, 2, 0, 1, 0, 1, 0, 8, 0 };
		uint8_t pal[128] = {0}; pal[0] = 0xFF; pal[3] = 0xAA; // color 1 = 0xAA0000FF
		uint8_t fr[] = { 0, 0, 1, 0, 1, 1, 1, 0 };            // pixels: 1, 0
		fwrite("PXD\x1A", 1, 4, f);
		fwrite("HEAD\x10\0\0\0", 1, 8, f); fwrite(head, 1, 16, f);
		fwrite("PALT\x80\0\0\0", 1, 8, f); fwrite(pal, 1, 128, f);
		fwrite("FRAM\x08\0\0\0", 1, 8, f); fwrite(fr, 1, 8, f);
		fwrite("END \0\0\0\0", 1, 8, f); fclose(f);
		Doc v = {0};
		assert(pxd_load(&v, "v1.pxd") == NULL);
		assert(v.colorCount == 32 && doc_color(&v, v.frames[0][0]) == 0xAA0000FF && v.frames[0][1] == 0);
		assert(v.swatches[0] == 0xAA0000FF);
		doc_free(&v);
	}

	// undo/redo
	Doc c = {0}; doc_new(&c, 4, 4);
	hist_push(&c); c.frames[0][0] = 1;
	hist_push(&c); c.frames[0][1] = 2;
	assert(hist_undo(&c) && c.frames[0][1] == 0 && c.frames[0][0] == 1);
	assert(hist_undo(&c) && c.frames[0][0] == 0);
	assert(!hist_undo(&c));
	assert(hist_redo(&c) && c.frames[0][0] == 1 && c.frames[0][1] == 0);
	assert(hist_redo(&c) && c.frames[0][1] == 2);
	assert(!hist_redo(&c));
	// empty stroke keeps redo
	assert(hist_undo(&c));
	hist_begin(&c); hist_abort();
	assert(hist_redo(&c) && c.frames[0][1] == 2);
	hist_begin(&c); c.frames[0][2] = 3; hist_commit();
	assert(hist_undo(&c) && c.frames[0][2] == 0);

	FILE* f = fopen("bad.pxd", "wb"); fwrite("PXD\x1A" "HEAD\xff\xff\xff\x7f", 1, 12, f); fclose(f);
	assert(pxd_load(&b, "bad.pxd") != NULL && b.w == 48);
	doc_free(&a); doc_free(&b); doc_free(&c); hist_clear();
	puts("ok");
	return 0;
}
