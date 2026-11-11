// Host-side test for .pxd save/load and undo history: cc -I source tests/test_pxd.c source/pxd.c source/doc.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doc.h"
#include "pxd.h"

// ---- hand-built files for the "untrusted input" checks ---------------------------
static uint8_t fb[4096];
static size_t fn;
static void chunk(const char* id, const void* body, uint32_t len)
{
	memcpy(fb + fn, id, 4);
	fb[fn + 4] = len; fb[fn + 5] = len >> 8; fb[fn + 6] = len >> 16; fb[fn + 7] = len >> 24;
	if (len < sizeof(fb)) memcpy(fb + fn + 8, body, len);
	fn += 8 + (len < sizeof(fb) ? len : 0);
}
static void f_begin(void) { memcpy(fb, "PXD\x1A", 4); fn = 4; }
static void f_head(int w, int h, int frames, int fps)
{
	uint8_t b[16] = { 4, 0, w, w >> 8, h, h >> 8, frames, 0, fps, fps >> 8 };
	chunk("HEAD", b, 16);
}
static void f_layer(int id, int folderPlus1, int flags)
{
	uint8_t b[24] = { id, 0, folderPlus1, 0, flags, 255 };
	chunk("LAYR", b, 24);
}
static void f_frame16(void) { uint8_t b[6] = { 0, 0, 1, 0, 16, 1 }; chunk("FRAM", b, 6); }   // 4x4 of color 1
static void f_write(const char* path) { FILE* f = fopen(path, "wb"); fwrite(fb, 1, fn, f); fclose(f); }

static void test_untrusted(void)
{
	static Doc u;
	static const uint8_t pal[4] = { 0xFF, 0, 0, 0xFF };
	uint8_t tags[1 + 16 * 21];
	memset(tags, 'A', sizeof(tags));
	tags[0] = 16;

	// a second HEAD must not change the size after frames were read
	f_begin(); f_head(4, 4, 1, 8); chunk("PALT", pal, 4); f_layer(1, 0, 2); f_frame16(); f_head(320, 240, 1, 8); chunk("END ", "", 0);
	f_write("hostile.pxd");
	assert(pxd_load(&u, "hostile.pxd") != NULL);

	// several TAGS chunks: never more than 16 tags, all inside the frame range, clean names
	f_begin(); f_head(4, 4, 1, 8); chunk("PALT", pal, 4); f_layer(1, 0, 2); f_frame16();
	for (int i = 0; i < 4; i++) chunk("TAGS", tags, sizeof(tags));
	chunk("END ", "", 0);
	f_write("hostile.pxd");
	assert(pxd_load(&u, "hostile.pxd") == NULL && u.tagCount == DOC_MAX_TAGS && u.layerCount == 1);
	for (int t = 0; t < u.tagCount; t++) assert(u.tags[t].from == 0 && u.tags[t].to == 0 && strlen(u.tags[t].name) == 15);

	// a "folder" link to a plain layer below is dropped; moving the real folder is then safe
	f_begin(); f_head(4, 4, 1, 8); chunk("PALT", pal, 4); f_layer(1, 0, 2); f_frame16(); f_layer(2, 0, 3); f_layer(3, 2, 2); f_frame16();
	chunk("END ", "", 0);
	f_write("hostile.pxd");
	assert(pxd_load(&u, "hostile.pxd") == NULL && u.layerCount == 3 && u.layers[2].folder == -1);
	assert(doc_layer_move(&u, 1, +1) == 2);

	// duplicate layer ids are refused
	f_begin(); f_head(4, 4, 1, 8); chunk("PALT", pal, 4); f_layer(1, 0, 2); f_frame16(); f_layer(1, 0, 2); f_frame16(); chunk("END ", "", 0);
	f_write("hostile.pxd");
	assert(pxd_load(&u, "hostile.pxd") != NULL);

	// a silly fps can't make a frame last 0 ms
	f_begin(); f_head(4, 4, 1, 2000); chunk("PALT", pal, 4); f_layer(1, 0, 2); f_frame16(); chunk("END ", "", 0);
	f_write("hostile.pxd");
	assert(pxd_load(&u, "hostile.pxd") == NULL && u.fps <= 100 && doc_frame_ms(&u, 0) >= 10);

	// the gallery's header reader stops at a chunk that claims to be bigger than the file
	f_begin(); f_head(4, 4, 1, 8); chunk("JUNK", "", 0xFFFFFFF8u);
	f_write("hostile.pxd");
	int w, h, layers, frames, tw, th;
	bool has;
	static uint32_t thumb[PXD_THUMB * PXD_THUMB];
	assert(pxd_read_info("hostile.pxd", &w, &h, &layers, &frames, &has, thumb, &tw, &th) && w == 4 && !has);
	remove("hostile.pxd");
	doc_free(&u);

	// undo brings a color back exactly even when the table filled up in between
	static Doc k;
	assert(doc_new(&k, 16, 16));
	uint8_t* p = doc_pixels(&k);
	for (int i = 0; i < 255; i++) p[i] = doc_color_index(&k, 0x000000FF | ((uint32_t)(i * 65793 + 0x010000) << 8), false);
	uint32_t was = doc_color(&k, p[0]);
	hist_begin_cel(&k); p[0] = 0; hist_commit();                     // the only pixel of that color goes
	uint8_t v = doc_color_index(&k, 0x12345FFF, true);                // table full: compaction drops it
	hist_begin_cel(&k); doc_pixels(&k)[0] = v; hist_commit();
	assert(hist_undo(&k) && hist_undo(&k) && doc_color(&k, doc_pixels(&k)[0]) == was);
	for (int i = 1; i < 255; i++) assert(doc_pixels(&k)[i] != 0);
	doc_free(&k);
	hist_clear();
}

int main(void)
{
	test_untrusted();
	Doc a = {0}, b = {0};
	assert(doc_new(&a, 48, 30));
	for (int i = 0; i < 48 * 30; i++) a.layers[0].cels[0][i] = (i * 7 / 13) % 33;
	a.colorCount = 32;
	for (int i = 1; i <= 32; i++) a.colors[i] = 0x01020300u * i | 0xFF;
	a.swatches[3] = 0x12345678;
	a.layers[0].cels[1] = calloc(48 * 30, 1); a.layers[0].cels[1][5] = 9; a.frameCount = 2;
	assert(pxd_save(&a, "test.pxd") == NULL);
	assert(pxd_load(&b, "test.pxd") == NULL);
	assert(b.w == 48 && b.h == 30 && b.frameCount == 2 && b.swatches[3] == 0x12345678 && b.colorCount == 32 && b.colors[7] == a.colors[7]);
	assert(!memcmp(a.layers[0].cels[0], b.layers[0].cels[0], 48 * 30) && !memcmp(a.layers[0].cels[1], b.layers[0].cels[1], 48 * 30));

	// color table: find/add, compaction when full, nearest fallback
	Doc e = {0}; doc_new(&e, 4, 1);
	assert(e.colorCount == 0);
	uint8_t red = doc_color_index(&e, 0xFF0000FF, true);
	assert(red == 1 && doc_color_index(&e, 0xFF0000FF, true) == 1 && e.colorCount == 1);
	for (int i = 0; i < 300; i++) e.layers[0].cels[0][i % 4] = doc_color_index(&e, 0x00000AFF + ((uint32_t)i << 8), true);
	assert(e.colorCount <= DOC_MAX_COLORS);
	for (int x = 0; x < 4; x++) assert(doc_color(&e, e.layers[0].cels[0][x]) == 0x00000AFF + ((uint32_t)(296 + x) << 8));
	doc_free(&e);

	// recolor: swap two colors at once, then merge into an existing color
	{
		Doc r = {0}; doc_new(&r, 3, 1);
		uint8_t A = doc_color_index(&r, 0xFF0000FF, true), B = doc_color_index(&r, 0x0000FFFF, true);
		r.layers[0].cels[0][0] = A; r.layers[0].cels[0][1] = B; r.layers[0].cels[0][2] = 0;
		uint32_t from[2] = { 0xFF0000FF, 0x0000FFFF }, to[2] = { 0x0000FFFF, 0xFF0000FF };
		assert(doc_recolor(&r, from, to, 2));
		assert(doc_color(&r, r.layers[0].cels[0][0]) == 0x0000FFFF && doc_color(&r, r.layers[0].cels[0][1]) == 0xFF0000FF);
		uint32_t f2 = 0x0000FFFF, t2 = 0xFF0000FF;   // now both pixels red -> same entry
		assert(doc_recolor(&r, &f2, &t2, 1));
		assert(r.layers[0].cels[0][0] == r.layers[0].cels[0][1] && doc_color(&r, r.layers[0].cels[0][0]) == 0xFF0000FF);
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
		assert(v.colorCount == 32 && doc_color(&v, v.layers[0].cels[0][0]) == 0xAA0000FF && v.layers[0].cels[0][1] == 0);
		assert(v.swatches[0] == 0xAA0000FF);
		doc_free(&v);
	}

	// undo/redo
	Doc c = {0}; doc_new(&c, 4, 4);
	hist_push(&c); c.layers[0].cels[0][0] = 1;
	hist_push(&c); c.layers[0].cels[0][1] = 2;
	assert(hist_undo(&c) && c.layers[0].cels[0][1] == 0 && c.layers[0].cels[0][0] == 1);
	assert(hist_undo(&c) && c.layers[0].cels[0][0] == 0);
	assert(!hist_undo(&c));
	assert(hist_redo(&c) && c.layers[0].cels[0][0] == 1 && c.layers[0].cels[0][1] == 0);
	assert(hist_redo(&c) && c.layers[0].cels[0][1] == 2);
	assert(!hist_redo(&c));
	// empty stroke keeps redo
	assert(hist_undo(&c));
	hist_begin(&c); hist_abort();
	assert(hist_redo(&c) && c.layers[0].cels[0][1] == 2);
	hist_begin(&c); c.layers[0].cels[0][2] = 3; hist_commit();
	assert(hist_undo(&c) && c.layers[0].cels[0][2] == 0);

	FILE* f = fopen("bad.pxd", "wb"); fwrite("PXD\x1A" "HEAD\xff\xff\xff\x7f", 1, 12, f); fclose(f);
	assert(pxd_load(&b, "bad.pxd") != NULL && b.w == 48);
	doc_free(&a); doc_free(&b); doc_free(&c); hist_clear();
	puts("ok");
	return 0;
}
