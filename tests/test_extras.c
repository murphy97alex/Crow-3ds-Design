// Host tests: settings file, button remapping, palette files, indexed PNG, sheet padding/trim,
// sprite sheet + GIF import, settings undo, cel copy, tile (wrap) drawing.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "anim.h"
#include "doc.h"
#include "export_png.h"
#include "import.h"
#include "input.h"
#include "palette_io.h"
#include "settings.h"
#include "tools.h"

static Doc d;

static void test_settings(void)
{
	int grid = 1, onion = 35, lefty = 0;
	Setting items[] = { { "grid", &grid, 0, 1 }, { "onion_pct", &onion, 10, 80 }, { "lefty", &lefty, 0, 1 } };
	char buf[128];
	size_t len = settings_format(items, 3, buf, sizeof(buf));
	assert(len == strlen(buf) && !strcmp(buf, "grid=1\nonion_pct=35\nlefty=0\n"));
	settings_parse(items, 3, "lefty=1\r\nonion_pct=999\nunknown=4\ngrid=zero\nnonsense\n=3\ngrid=0");
	assert(lefty == 1 && onion == 35 && grid == 0);        // out of range and junk are ignored
	char tiny[10];
	assert(settings_format(items, 3, tiny, sizeof(tiny)) == 7 && !strcmp(tiny, "grid=0\n"));
	assert(settings_save("/tmp/pc_settings.txt", items, 3));
	grid = 1; lefty = 0; onion = 50;
	assert(settings_load("/tmp/pc_settings.txt", items, 3) && grid == 0 && lefty == 1 && onion == 35);
	assert(!settings_load("/tmp/pc_no_such_file.txt", items, 3));
}

static void test_input(void)
{
	assert(input_mirror(IN_DUP | IN_L | IN_ZR | IN_TOUCH | IN_START) == (IN_X | IN_R | IN_ZL | IN_TOUCH | IN_START));
	assert(input_mirror(input_mirror(0xFFFFFFFFu)) == 0xFFFFFFFFu);
	assert(input_mirror(IN_A) == IN_DLEFT && input_mirror(IN_Y) == IN_DRIGHT && input_mirror(IN_B) == IN_DDOWN);

	InputMap m = { .lefty = true };
	uint32_t down = IN_R, held = IN_R | IN_ZL, up = 0;
	input_map(&m, &down, &held, &up);
	assert(down == IN_L && held == (IN_L | IN_ZR));

	// Old 3DS: a SELECT tap arrives when it is let go
	InputMap o = { .old3ds = true };
	down = IN_SELECT; held = IN_SELECT; up = 0;
	input_map(&o, &down, &held, &up);
	assert(down == 0 && held == IN_ZR);
	down = 0; held = 0; up = IN_SELECT;
	input_map(&o, &down, &held, &up);
	assert(down == IN_SELECT && held == 0);
	// ...but not when it was held with the stylus down (used as ZR)
	down = IN_SELECT; held = IN_SELECT; up = 0;
	input_map(&o, &down, &held, &up);
	down = IN_TOUCH; held = IN_SELECT | IN_TOUCH; up = 0;
	input_map(&o, &down, &held, &up);
	assert(held == (IN_ZR | IN_TOUCH) && down == IN_TOUCH);
	down = 0; held = 0; up = IN_SELECT | IN_TOUCH;
	input_map(&o, &down, &held, &up);
	assert(!(down & IN_SELECT));
	// R + L = redo; L alone stays undo
	down = IN_L; held = IN_L | IN_R; up = 0;
	input_map(&o, &down, &held, &up);
	assert(down == IN_ZL);
	down = IN_L; held = IN_L; up = 0;
	input_map(&o, &down, &held, &up);
	assert(down == IN_L);
}

static void test_palette(void)
{
	uint32_t cols[4] = { 0xFF0000FF, 0x00FF00FF, 0x0000FFFF, 0x102030FF }, in[8];
	assert(!palette_save_hex("/tmp/pc_pal.hex", cols, 4));
	assert(palette_load("/tmp/pc_pal.hex", in, 8) == 4 && !memcmp(in, cols, sizeof(cols)));
	assert(!palette_save_gpl("/tmp/pc_pal.gpl", "test", cols, 4));
	assert(palette_load("/tmp/pc_pal.gpl", in, 8) == 4 && !memcmp(in, cols, sizeof(cols)));
	assert(palette_load("/tmp/pc_pal.gpl", in, 2) == 2);
	assert(palette_parse("#AbCdEf\n  123456ff \nnope\n12345\n1234567\n\nffffff", in, 8) == 3);
	assert(in[0] == 0xABCDEFFF && in[1] == 0x123456FF && in[2] == 0xFFFFFFFF);
	assert(palette_parse("GIMP Palette\nName: x\nColumns: 4\n# c\n255 0 10\tred\n300 0 0\n1 2\n 0   0 0 Untitled\n", in, 8) == 2);
	assert(in[0] == 0xFF000AFF && in[1] == 0x000000FF);
	assert(palette_load("/tmp/pc_no_such_file.hex", in, 8) == 0);
}

static void test_exports(void)
{
	// 4x4 canvas, 3 frames, a 2x2 block in the middle that changes color
	assert(doc_new(&d, 4, 4));
	uint8_t red = doc_color_index(&d, 0xFF0000FF, true), blue = doc_color_index(&d, 0x0000FFFF, true);
	assert(doc_frame_insert(&d, 1, -1) && doc_frame_insert(&d, 2, -1));
	for (int f = 0; f < 3; f++)
		for (int y = 1; y <= 2; y++)
			for (int x = 1; x <= 2; x++) d.layers[0].cels[f][y * 4 + x] = f == 1 ? blue : red;
	d.layers[0].cels[2][1 * 4 + 1] = blue;

	// indexed PNG: libpng reads it back as the same picture
	uint32_t* img; int w, h; bool shrunk;
	assert(!png_export_indexed(&d, 2, 3, "/tmp/pc_idx.png"));
	assert(!import_read_png("/tmp/pc_idx.png", 4096, 4096, &img, &w, &h, &shrunk));
	assert(w == 12 && h == 12 && img[0] == 0 && img[3 * 12 + 3] == 0x0000FFFF && img[3 * 12 + 6] == 0xFF0000FF && img[11 * 12 + 11] == 0);
	free(img);
	{
		FILE* f = fopen("/tmp/pc_idx.png", "rb");
		uint8_t hd[64];
		assert(fread(hd, 1, 64, f) == 64);
		fclose(f);
		assert(hd[25] == 3 && !memcmp(hd + 37, "PLTE", 4) && hd[36] == 9);   // palette type, 3 entries
	}

	// sheet with padding and trim: 3 cells of 2x2 at 2x, 3 px apart
	SheetOpts o = { SHEET_ROW, 2, 3, true };
	assert(anim_sheet_size(&d, -1, &o, &w, &h) && w == 3 * 4 + 2 * 3 && h == 4);
	assert(!anim_export_sheet(&d, -1, &o, "/tmp/pc_trim.png"));
	assert(!import_read_png("/tmp/pc_trim.png", 4096, 4096, &img, &w, &h, &shrunk));
	assert(w == 18 && h == 4);
	assert(img[0] == 0xFF0000FF && img[3] == 0xFF0000FF && img[4] == 0 && img[6] == 0);      // cell 0, then padding
	assert(img[7] == 0x0000FFFF && img[14] == 0x0000FFFF && img[16] == 0xFF0000FF);          // cell 1, cell 2
	free(img);
	strcpy(d.name, "t");
	assert(!anim_export_sheet_json(&d, -1, &o, "t.png", "/tmp/pc_trim.json"));
	{
		FILE* jf = fopen("/tmp/pc_trim.json", "r");
		static char js[8192];
		size_t jn = fread(js, 1, sizeof(js) - 1, jf);
		fclose(jf);
		js[jn] = 0;
		assert(strstr(js, "\"frame\": { \"x\": 7, \"y\": 0, \"w\": 4, \"h\": 4 }"));
		assert(strstr(js, "\"trimmed\": true") && strstr(js, "\"spriteSourceSize\": { \"x\": 2, \"y\": 2, \"w\": 4, \"h\": 4 }"));
		assert(strstr(js, "\"sourceSize\": { \"w\": 8, \"h\": 8 }") && strstr(js, "\"size\": { \"w\": 18, \"h\": 4 }"));
	}
	// padded grid without trim
	SheetOpts g = { SHEET_GRID, 1, 1, false };
	assert(anim_sheet_size(&d, -1, &g, &w, &h) && w == 9 && h == 9);
	assert(!anim_export_sheet(&d, -1, &g, "/tmp/pc_grid.png"));
	assert(!import_read_png("/tmp/pc_grid.png", 4096, 4096, &img, &w, &h, &shrunk));
	assert(img[1 * 9 + 1] == 0xFF0000FF && img[1 * 9 + 4] == 0 && img[1 * 9 + 6] == 0x0000FFFF && img[6 * 9 + 1] == 0x0000FFFF);
	free(img);
}

static void test_import_anim(void)
{
	// the sheet written above (one row, no padding) comes back as the same 3 frames
	SheetOpts o = { SHEET_GRID, 1, 0, false };
	assert(!anim_export_sheet(&d, -1, &o, "/tmp/pc_rt.png"));        // 2x2 grid: the 4th cell is empty
	uint32_t* img; int w, h; bool shrunk;
	assert(!import_read_png("/tmp/pc_rt.png", 4096, 4096, &img, &w, &h, &shrunk));
	static Doc s;
	assert(import_sheet_doc(&s, img, w, h, 0, 1) != NULL && import_sheet_doc(&s, img, w, h, 9, 9) != NULL);
	assert(!import_sheet_doc(&s, img, w, h, 2, 2));
	free(img);
	assert(s.w == 4 && s.h == 4 && s.frameCount == 3 && s.colorCount == 2);
	uint32_t a[16], b[16];
	for (int f = 0; f < 3; f++)
	{
		doc_composite(&d, f, a);
		doc_composite(&s, f, b);
		assert(!memcmp(a, b, sizeof(a)));
	}

	// GIF round trip, with durations
	d.frameMs[1] = 300;
	assert(!gif_export(&d, -1, 1, "/tmp/pc_rt.gif"));
	bool cut = true;
	assert(!import_gif_doc(&s, "/tmp/pc_rt.gif", &cut) && !cut);
	assert(s.w == 4 && s.h == 4 && s.frameCount == 3 && s.frameMs[1] == 300 && s.frameMs[0] == 130);   // GIF times are in 1/100 s
	for (int f = 0; f < 3; f++)
	{
		doc_composite(&d, f, a);
		doc_composite(&s, f, b);
		assert(!memcmp(a, b, sizeof(a)));
	}
	// a scaled GIF bigger than a canvas is shrunk to fit
	assert(!gif_export(&d, -1, 100, "/tmp/pc_big.gif"));
	assert(!import_gif_doc(&s, "/tmp/pc_big.gif", &cut) && s.w == 240 && s.h == 240 && s.frameCount == 3);
	assert(doc_color(&s, s.layers[0].cels[1][120 * 240 + 120]) == 0x0000FFFF && s.layers[0].cels[1][0] == 0);
	// junk is refused and leaves the project alone
	assert(import_gif_mem(&s, (const uint8_t*)"GIF89a\1\0\1\0\0\0\0;", 14, &cut) != NULL && s.w == 240);
	assert(import_gif_mem(&s, (const uint8_t*)"NOPE", 4, &cut) != NULL);
	doc_free(&s);
}

static void test_doc_extras(void)
{
	// rectangle composite matches the full one
	static uint32_t full[16], part[16];
	doc_composite(&d, 2, full);
	memset(part, 0x55, sizeof(part));
	doc_composite_rect(&d, 2, part, 1, 1, 9, 9);
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
			assert(part[y * 4 + x] == (x >= 1 && y >= 1 ? full[y * 4 + x] : 0x55555555u));

	// a one-frame tag follows its frame; a longer tag keeps its place
	d.tagCount = 2;
	d.tags[0] = (Tag){ "hit", 0, 0, TAG_FORWARD };
	d.tags[1] = (Tag){ "walk", 0, 1, TAG_FORWARD };
	d.curFrame = 0;
	assert(doc_frame_move(&d, 0, +1) && d.curFrame == 1);
	assert(d.tags[0].from == 1 && d.tags[0].to == 1 && d.tags[1].from == 0 && d.tags[1].to == 1);
	assert(doc_frame_move(&d, 1, -1) && d.tags[0].from == 0);

	// copy a cel to the next frame (unlinks it first)
	assert(doc_cel_link_prev(&d, 0, 2) && d.layers[0].cels[2] == d.layers[0].cels[1]);
	assert(doc_cel_copy_next(&d, 0, 0));
	assert(d.layers[0].cels[1] != d.layers[0].cels[2] && !memcmp(d.layers[0].cels[0], d.layers[0].cels[1], 16));
	assert(memcmp(d.layers[0].cels[1], d.layers[0].cels[2], 16));    // frame 2 kept the old drawing
	assert(doc_cel_unlink(&d, 0, 2) && doc_cel_copy_next(&d, 0, 1) && !doc_cel_copy_next(&d, 0, 2));
	assert(d.layers[0].cels[1] != d.layers[0].cels[2] && d.layers[0].cels[1] != d.layers[0].cels[0]);

	// settings undo: swatches, tags, durations, layer properties; repeated changes share a step
	hist_clear();
	uint32_t sw0 = d.swatches[0];
	hist_push_meta(&d, 0); d.swatches[0] = 0x010203FF;
	hist_push_meta(&d, 7); d.layers[0].opacity = 200;
	hist_push_meta(&d, 7); d.layers[0].opacity = 100;       // same key: same step
	hist_push_meta(&d, 8); d.frameMs[0] = 500; d.tagCount = 0; strcpy(d.layers[0].name, "renamed");
	assert(hist_undo(&d) && d.frameMs[0] != 500 && d.tagCount == 2 && strcmp(d.layers[0].name, "renamed") && d.layers[0].opacity == 100);
	assert(hist_undo(&d) && d.layers[0].opacity == 255 && d.swatches[0] == 0x010203FF);
	assert(hist_undo(&d) && d.swatches[0] == sw0 && !hist_undo(&d));
	assert(hist_redo(&d) && hist_redo(&d) && hist_redo(&d) && !hist_redo(&d));
	assert(d.swatches[0] == 0x010203FF && d.layers[0].opacity == 100 && d.frameMs[0] == 500 && d.tagCount == 0);
	// a whole-document step brings the swatches back too
	hist_push(&d); d.swatches[1] = 0x0A0B0CFF;
	assert(hist_undo(&d) && d.swatches[1] != 0x0A0B0CFF);
	// tags undone after frames were removed stay in range
	d.tagCount = 1; d.tags[0] = (Tag){ "end", 2, 2, TAG_FORWARD };
	hist_clear();
	hist_push_meta(&d, 0); d.tagCount = 0;
	assert(doc_frame_delete(&d, 2) && doc_frame_delete(&d, 1));
	assert(hist_undo(&d) && d.tagCount == 1 && d.tags[0].from == 0 && d.tags[0].to == 0);
	hist_clear();
}

static void test_wrap(void)
{
	assert(doc_new(&d, 8, 8));
	Brush b = { 1, BRUSH_SQUARE, false };
	tools_begin(&d, MIRROR_OFF);
	tools_line(6, 3, 10, 3, 1, &b);                // runs off the right edge
	assert(doc_get(&d, 7, 3) == 1 && doc_get(&d, 0, 3) == 0);
	tools_set_wrap(true);
	tools_begin(&d, MIRROR_OFF);
	tools_line(6, 5, 10, 5, 1, &b);
	tools_line(2, -2, 2, 1, 1, &b);
	assert(doc_get(&d, 7, 5) == 1 && doc_get(&d, 0, 5) == 1 && doc_get(&d, 2, 5) == 1 && doc_get(&d, 3, 5) == 0);
	assert(doc_get(&d, 2, 6) == 1 && doc_get(&d, 2, 7) == 1 && doc_get(&d, 2, 0) == 1 && doc_get(&d, 2, 2) == 0);
	tools_set_wrap(false);
}

int main(void)
{
	test_settings();
	test_input();
	test_palette();
	test_exports();
	test_import_anim();
	test_doc_extras();
	test_wrap();
	doc_free(&d);
	hist_clear();
	puts("ok");
	return 0;
}
