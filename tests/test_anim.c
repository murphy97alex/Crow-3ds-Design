// Host tests: playback order, sprite sheet, numbered frames, animated GIF (decoded here).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "anim.h"
#include "doc.h"
#include "import.h"

// ---- tiny GIF decoder (enough to check our own output) ---------------------------

typedef struct { int w, h, frames; int delay[ANIM_MAX_SEQ]; uint32_t* px[ANIM_MAX_SEQ]; bool loops; } Gif;

static const uint8_t* subblocks(const uint8_t* p, uint8_t* out, size_t* len)
{
	*len = 0;
	while (*p) { int n = *p++; if (out) memcpy(out + *len, p, n); *len += n; p += n; }
	return p + 1;
}

static void lzw_decode(const uint8_t* data, size_t len, int minCode, uint8_t* out, size_t outLen)
{
	static uint16_t prefix[4096];
	static uint8_t suffix[4096], stack[4096];
	int clear = 1 << minCode, eoi = clear + 1, codeSize = minCode + 1, next = eoi + 1, prev = -1;
	uint8_t first = 0;
	size_t bitPos = 0, o = 0;
	for (int i = 0; i < clear; i++) { prefix[i] = 0xFFFF; suffix[i] = (uint8_t)i; }
	for (;;)
	{
		assert(bitPos + codeSize <= len * 8);
		int code = 0;
		for (int b = 0; b < codeSize; b++, bitPos++) code |= ((data[bitPos >> 3] >> (bitPos & 7)) & 1) << b;
		if (code == clear) { codeSize = minCode + 1; next = eoi + 1; prev = -1; continue; }
		if (code == eoi) break;
		int sp = 0, c = code;
		if (prev < 0) { assert(code < clear); out[o++] = (uint8_t)code; prev = code; first = (uint8_t)code; continue; }
		if (code >= next) { assert(code == next); stack[sp++] = first; c = prev; }
		while (c >= clear) { stack[sp++] = suffix[c]; c = prefix[c]; }
		stack[sp++] = (uint8_t)c;
		first = (uint8_t)c;
		while (sp) { assert(o < outLen); out[o++] = stack[--sp]; }
		if (next < 4096) { prefix[next] = (uint16_t)prev; suffix[next] = first; next++; if (next == (1 << codeSize) && codeSize < 12) codeSize++; }
		prev = code;
	}
	assert(o == outLen);
}

static void read_gif(const char* path, Gif* g)
{
	FILE* f = fopen(path, "rb");
	assert(f);
	static uint8_t buf[1 << 22], data[1 << 22];
	size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	assert(n > 13 && !memcmp(buf, "GIF89a", 6));
	memset(g, 0, sizeof(*g));
	g->w = buf[6] | buf[7] << 8;
	g->h = buf[8] | buf[9] << 8;
	assert(buf[10] & 0x80);
	int palN = 2 << (buf[10] & 7);
	const uint8_t* pal = buf + 13;
	const uint8_t* p = pal + palN * 3;
	int delay = 0, trans = -1;
	for (;;)
	{
		if (*p == 0x3B) break;
		if (*p == 0x21)
		{
			if (p[1] == 0xF9) { delay = p[4] | p[5] << 8; trans = (p[3] & 1) ? p[6] : -1; }
			if (p[1] == 0xFF && !memcmp(p + 3, "NETSCAPE2.0", 11)) g->loops = true;
			size_t l;
			p = subblocks(p + 2 + 1 + p[2], NULL, &l);
			continue;
		}
		assert(*p == 0x2C);
		int w = p[5] | p[6] << 8, h = p[7] | p[8] << 8;
		assert(w == g->w && h == g->h && p[9] == 0);
		int minCode = p[10];
		size_t l;
		p = subblocks(p + 11, data, &l);
		uint8_t* idx = malloc((size_t)w * h);
		lzw_decode(data, l, minCode, idx, (size_t)w * h);
		uint32_t* px = malloc((size_t)w * h * 4);
		for (int i = 0; i < w * h; i++)
		{
			int k = idx[i];
			px[i] = k == trans ? 0 : ((uint32_t)pal[k * 3] << 24 | pal[k * 3 + 1] << 16 | pal[k * 3 + 2] << 8 | 255);
		}
		free(idx);
		g->delay[g->frames] = delay;
		g->px[g->frames++] = px;
	}
}

static void free_gif(Gif* g) { for (int i = 0; i < g->frames; i++) free(g->px[i]); }

static Doc d;

int main(void)
{
	assert(doc_new(&d, 5, 3));
	uint8_t red = doc_color_index(&d, 0xFF0000FF, true), blue = doc_color_index(&d, 0x0000FFFF, true);
	assert(doc_frame_insert(&d, 1, -1) && doc_frame_insert(&d, 2, -1));
	for (int f = 0; f < 3; f++) d.layers[0].cels[f][f] = f == 1 ? blue : red;  // a pixel walks right
	d.frameMs[1] = 300;

	// playback order
	int seq[ANIM_MAX_SEQ];
	assert(anim_sequence(&d, -1, seq) == 3 && seq[0] == 0 && seq[2] == 2);
	d.tagCount = 2;
	d.tags[0] = (Tag){ "pp", 0, 2, TAG_PINGPONG };
	d.tags[1] = (Tag){ "rev", 1, 2, TAG_REVERSE };
	assert(anim_sequence(&d, 0, seq) == 4 && seq[0] == 0 && seq[1] == 1 && seq[2] == 2 && seq[3] == 1);
	assert(anim_sequence(&d, 1, seq) == 2 && seq[0] == 2 && seq[1] == 1);
	int c, r;
	anim_sheet_grid(5, SHEET_GRID, &c, &r);
	assert(c == 3 && r == 2);

	// sprite sheet: 3 frames in a row at 2x
	assert(!anim_export_sheet(&d, -1, &(SheetOpts){ SHEET_ROW, 2, 0, false }, "/tmp/pc_sheet.png"));
	uint32_t* img; int w, h; bool shrunk;
	assert(!import_read_png("/tmp/pc_sheet.png", 4096, 4096, &img, &w, &h, &shrunk));
	assert(w == 30 && h == 6);
	assert(img[0] == 0xFF0000FF && img[1] == 0xFF0000FF && img[2] == 0);       // frame 0, pixel 0
	assert(img[10 + 2] == 0x0000FFFF && img[10] == 0);                          // frame 1, pixel 1
	assert(img[20 + 4] == 0xFF0000FF && img[30 + 25] == 0xFF0000FF);           // frame 2, pixel 2 (row 1 too)
	free(img);
	// grid of the reverse tag (2 frames -> 2x1), column layout
	assert(!anim_export_sheet(&d, 1, &(SheetOpts){ SHEET_COLUMN, 1, 0, false }, "/tmp/pc_col.png"));
	assert(!import_read_png("/tmp/pc_col.png", 4096, 4096, &img, &w, &h, &shrunk));
	assert(w == 5 && h == 6 && img[1] == 0x0000FFFF && img[15 + 2] == 0xFF0000FF);
	free(img);
	assert(anim_export_sheet(&d, -1, &(SheetOpts){ SHEET_ROW, 200, 0, false }, "/tmp/pc_big.png"));       // too big

	// JSON data file for a sheet (Aseprite array layout)
	strcpy(d.name, "hero");
	assert(!anim_export_sheet_json(&d, -1, &(SheetOpts){ SHEET_GRID, 2, 0, false }, "hero_sheet_2x.png", "/tmp/pc_sheet.json"));
	{
		FILE* jf = fopen("/tmp/pc_sheet.json", "r");
		static char js[8192];
		size_t jn = fread(js, 1, sizeof(js) - 1, jf);
		fclose(jf);
		js[jn] = 0;
		// 3 frames in a 2x2 grid of 10x6 cells
		assert(strstr(js, "\"frame\": { \"x\": 10, \"y\": 0, \"w\": 10, \"h\": 6 }"));
		assert(strstr(js, "\"frame\": { \"x\": 0, \"y\": 6, \"w\": 10, \"h\": 6 }"));
		assert(strstr(js, "\"duration\": 300"));
		assert(strstr(js, "\"size\": { \"w\": 20, \"h\": 12 }"));
		assert(strstr(js, "{ \"name\": \"pp\", \"from\": 0, \"to\": 2, \"direction\": \"pingpong\" }"));
		assert(strstr(js, "\"image\": \"hero_sheet_2x.png\""));
	}
	// only tags inside the range, renumbered
	assert(!anim_export_sheet_json(&d, 1, &(SheetOpts){ SHEET_ROW, 1, 0, false }, "x.png", "/tmp/pc_tag.json"));
	{
		FILE* jf = fopen("/tmp/pc_tag.json", "r");
		static char js[8192];
		size_t jn = fread(js, 1, sizeof(js) - 1, jf);
		fclose(jf);
		js[jn] = 0;
		assert(!strstr(js, "\"pp\"") && strstr(js, "\"name\": \"rev\", \"from\": 0, \"to\": 1, \"direction\": \"reverse\""));
	}

	// numbered frames
	assert(!anim_export_frames(&d, -1, 1, "/tmp/pc_frame"));
	assert(!import_read_png("/tmp/pc_frame_003.png", 4096, 4096, &img, &w, &h, &shrunk));
	assert(w == 5 && h == 3 && img[2] == 0xFF0000FF && img[0] == 0);
	free(img);

	// GIF, ping-pong tag at 3x
	Gif g;
	assert(!gif_export(&d, 0, 3, "/tmp/pc_anim.gif"));
	read_gif("/tmp/pc_anim.gif", &g);
	assert(g.loops && g.w == 15 && g.h == 9 && g.frames == 4);
	assert(g.delay[0] == 13 && g.delay[1] == 30 && g.delay[3] == 30);           // 125 ms -> 13 cs
	assert(g.px[0][0] == 0xFF0000FF && g.px[0][2] == 0xFF0000FF && g.px[0][3] == 0);
	assert(g.px[1][3] == 0x0000FFFF && g.px[1][0] == 0);
	assert(g.px[2][6] == 0xFF0000FF && g.px[3][15 * 2 + 5] == 0x0000FFFF);
	free_gif(&g);

	// a bigger image with many colors (forces LZW table resets and the >255 colors fallback)
	doc_free(&d);
	assert(doc_new(&d, 200, 150));
	for (int i = 1; i <= 200; i++) doc_color_index(&d, 0xFF000000u | (uint32_t)(i * 1237 % 65536) << 8 | 255, true);
	srand(1);
	for (int i = 0; i < 200 * 150; i++) d.layers[0].cels[0][i] = (uint8_t)(rand() % (d.colorCount + 1));
	int l2 = doc_layer_add(&d, false);
	d.layers[l2].opacity = 128;  // blending makes many more than 255 colors
	for (int i = 0; i < 200 * 150; i += 3) d.layers[l2].cels[0][i] = (uint8_t)(1 + rand() % d.colorCount);
	assert(!gif_export(&d, -1, 1, "/tmp/pc_many.gif"));
	read_gif("/tmp/pc_many.gif", &g);
	assert(g.frames == 1 && g.w == 200);
	uint32_t* comp = malloc(200 * 150 * 4);
	doc_composite(&d, 0, comp);
	for (int i = 0; i < 200 * 150; i++)
		assert(((comp[i] & 255) < 128) == (g.px[0][i] == 0));    // transparency matches
	free(comp);
	free_gif(&g);
	// exact colors survive when under 255 colors
	d.layers[l2].visible = false;
	assert(!gif_export(&d, -1, 1, "/tmp/pc_exact.gif"));
	read_gif("/tmp/pc_exact.gif", &g);
	comp = malloc(200 * 150 * 4);
	doc_composite(&d, 0, comp);
	for (int i = 0; i < 200 * 150; i++) assert(comp[i] == g.px[0][i]);
	free(comp);
	free_gif(&g);
	doc_free(&d);
	puts("ok");
	return 0;
}
