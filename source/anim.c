// SPDX-License-Identifier: MIT
#include "anim.h"
#include "export_png.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void anim_range(const Doc* d, int tag, int* from, int* to)
{
	*from = 0;
	*to = d->frameCount - 1;
	if (tag >= 0 && tag < d->tagCount)
	{
		const Tag* t = &d->tags[tag];
		int a = t->from, b = t->to;
		if (a > b) { int s = a; a = b; b = s; }
		if (a >= d->frameCount) a = d->frameCount - 1;
		if (b >= d->frameCount) b = d->frameCount - 1;
		*from = a;
		*to = b;
	}
}

int anim_sequence(const Doc* d, int tag, int* out)
{
	int from, to, n = 0;
	anim_range(d, tag, &from, &to);
	int dir = (tag >= 0 && tag < d->tagCount) ? d->tags[tag].dir : TAG_FORWARD;
	if (dir == TAG_REVERSE)
		for (int f = to; f >= from; f--) out[n++] = f;
	else
	{
		for (int f = from; f <= to; f++) out[n++] = f;
		if (dir == TAG_PINGPONG)
			for (int f = to - 1; f > from; f--) out[n++] = f;
	}
	return n;
}

void anim_sheet_grid(int n, SheetLayout layout, int* cols, int* rows)
{
	if (n < 1) n = 1;
	if (layout == SHEET_ROW) { *cols = n; *rows = 1; }
	else if (layout == SHEET_COLUMN) { *cols = 1; *rows = n; }
	else
	{
		int c = 1;
		while (c * c < n) c++;
		*cols = c;
		*rows = (n + c - 1) / c;
	}
}

// ---- sprite sheet ---------------------------------------------------------------

typedef struct {
	const Doc* d;
	int from, n, cols, scale;
	uint32_t* buf;   // composites of one row of frames: cols * w * h
	int bufRow;
} SheetCtx;

static void sheet_row(void* ctx, int y, uint32_t* row)
{
	SheetCtx* s = ctx;
	const Doc* d = s->d;
	int fw = d->w * s->scale, fh = d->h * s->scale;
	int r = y / fh;
	if (r != s->bufRow)
	{
		for (int c = 0; c < s->cols; c++)
		{
			int k = r * s->cols + c;
			uint32_t* dst = s->buf + (size_t)c * d->w * d->h;
			if (k < s->n) doc_composite(d, s->from + k, dst);
			else memset(dst, 0, (size_t)d->w * d->h * 4);
		}
		s->bufRow = r;
	}
	int fy = (y % fh) / s->scale;
	for (int c = 0; c < s->cols; c++)
	{
		const uint32_t* src = s->buf + (size_t)c * d->w * d->h + (size_t)fy * d->w;
		uint32_t* dst = row + c * fw;
		for (int x = 0; x < fw; x++) dst[x] = src[x / s->scale];
	}
}

const char* anim_export_sheet(const Doc* d, int tag, SheetLayout layout, int scale, const char* path)
{
	int from, to;
	anim_range(d, tag, &from, &to);
	int n = to - from + 1, cols, rows;
	anim_sheet_grid(n, layout, &cols, &rows);
	if (scale < 1) scale = 1;
	long W = (long)cols * d->w * scale, H = (long)rows * d->h * scale;
	if (W > PNG_MAX_DIM || H > PNG_MAX_DIM) return "Sheet too big";
	SheetCtx s = { d, from, n, cols, scale, malloc((size_t)cols * d->w * d->h * 4), -1 };
	if (!s.buf) return "Out of memory";
	const char* err = pngx_write_rows((int)W, (int)H, sheet_row, &s, path);
	free(s.buf);
	return err;
}

const char* anim_export_frames(const Doc* d, int tag, int scale, const char* base)
{
	int from, to;
	anim_range(d, tag, &from, &to);
	if (scale < 1) scale = 1;
	uint32_t* buf = malloc((size_t)d->w * d->h * 4);
	if (!buf) return "Out of memory";
	const char* err = NULL;
	char path[256];
	for (int f = from; f <= to && !err; f++)
	{
		snprintf(path, sizeof(path), "%s_%03d.png", base, f - from + 1);
		doc_composite(d, f, buf);
		err = pngx_write_rgba(buf, d->w, d->h, d->w * scale, d->h * scale, path);
	}
	free(buf);
	return err;
}

// ---- GIF ------------------------------------------------------------------------

// Color lookup: 0xRRGGBB -> palette index (open addressing).
#define CMAP_SIZE 65536
typedef struct {
	uint32_t* keys;  // rgb | 0x01000000 (0 = empty)
	uint8_t* vals;
	int used;
} ColorMap;

static int cmap_find(const ColorMap* m, uint32_t rgb, int* slot)
{
	uint32_t key = rgb | 0x01000000u;
	uint32_t h = (rgb * 2654435761u) >> 16;
	for (int i = 0; i < CMAP_SIZE; i++)
	{
		uint32_t s = (h + i) & (CMAP_SIZE - 1);
		if (m->keys[s] == key) { *slot = s; return m->vals[s]; }
		if (!m->keys[s]) { *slot = s; return -1; }
	}
	*slot = -1;
	return -1;
}

static void cmap_put(ColorMap* m, int slot, uint32_t rgb, int v)
{
	if (slot < 0 || m->used > CMAP_SIZE / 2) return;  // full: just don't cache
	m->keys[slot] = rgb | 0x01000000u;
	m->vals[slot] = (uint8_t)v;
	m->used++;
}

typedef struct {
	uint8_t pal[256 * 3];
	int count;          // entries used, including transparent index 0
	bool nearest;       // too many colors: map to the nearest palette entry
	ColorMap map;
} GifPalette;

static int nearest_index(const GifPalette* p, uint32_t rgb)
{
	int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
	int best = 1, bestD = 1 << 30;
	for (int i = 1; i < p->count; i++)
	{
		int dr = r - p->pal[i * 3], dg = g - p->pal[i * 3 + 1], db = b - p->pal[i * 3 + 2];
		int dd = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (dd < bestD) { bestD = dd; best = i; }
	}
	return best;
}

static int pal_index(GifPalette* p, uint32_t c)
{
	if ((c & 255) < 128) return 0;
	uint32_t rgb = c >> 8;
	int slot;
	int v = cmap_find(&p->map, rgb, &slot);
	if (v >= 0) return v;
	if (!p->nearest && p->count < 256)
	{
		v = p->count++;
		p->pal[v * 3] = rgb >> 16;
		p->pal[v * 3 + 1] = rgb >> 8;
		p->pal[v * 3 + 2] = rgb;
	}
	else if (!p->nearest) return -1;  // overflow while collecting
	else v = p->count > 1 ? nearest_index(p, rgb) : 0;
	cmap_put(&p->map, slot, rgb, v);
	return v;
}

typedef struct {
	FILE* f;
	uint8_t block[256];
	int blockLen;
	uint32_t bits;
	int bitCount;
	bool ok;
} BitOut;

static void put_byte(BitOut* o, uint8_t b)
{
	o->block[o->blockLen++] = b;
	if (o->blockLen == 255)
	{
		uint8_t n = 255;
		o->ok = o->ok && fwrite(&n, 1, 1, o->f) == 1 && fwrite(o->block, 1, 255, o->f) == 255;
		o->blockLen = 0;
	}
}

static void put_code(BitOut* o, int code, int size)
{
	o->bits |= (uint32_t)code << o->bitCount;
	o->bitCount += size;
	while (o->bitCount >= 8)
	{
		put_byte(o, o->bits & 255);
		o->bits >>= 8;
		o->bitCount -= 8;
	}
}

static void flush_bits(BitOut* o)
{
	if (o->bitCount > 0) put_byte(o, o->bits & 255);
	o->bits = 0;
	o->bitCount = 0;
	if (o->blockLen)
	{
		uint8_t n = (uint8_t)o->blockLen;
		o->ok = o->ok && fwrite(&n, 1, 1, o->f) == 1 && fwrite(o->block, 1, o->blockLen, o->f) == (size_t)o->blockLen;
		o->blockLen = 0;
	}
	uint8_t z = 0;
	o->ok = o->ok && fwrite(&z, 1, 1, o->f) == 1;
}

#define LZW_HASH 8192
typedef struct { int32_t key[LZW_HASH]; int16_t code[LZW_HASH]; } LzwTable;

// LZW-compress the frame's indices (sw x sh, scaled by `scale`) as one GIF image data block.
static void lzw_frame(BitOut* o, LzwTable* t, const uint8_t* idx, int sw, int sh, int scale, int minCode)
{
	int clear = 1 << minCode, eoi = clear + 1;
	int codeSize = minCode + 1, maxCode = eoi;
	memset(t->key, 0xff, sizeof(t->key));
	uint8_t mc = (uint8_t)minCode;
	o->ok = o->ok && fwrite(&mc, 1, 1, o->f) == 1;
	put_code(o, clear, codeSize);

	int W = sw * scale, H = sh * scale;
	int cur = -1;
	for (int y = 0; y < H; y++)
	{
		const uint8_t* row = idx + (size_t)(y / scale) * sw;
		for (int x = 0; x < W; x++)
		{
			int k = row[x / scale];
			if (cur < 0) { cur = k; continue; }
			int32_t key = (cur << 8) | k;
			uint32_t h = ((uint32_t)key * 2654435761u) >> 19;
			int found = -1;
			for (;;)
			{
				h &= LZW_HASH - 1;
				if (t->key[h] == key) { found = t->code[h]; break; }
				if (t->key[h] < 0) break;
				h++;
			}
			if (found >= 0) { cur = found; continue; }
			put_code(o, cur, codeSize);
			maxCode++;
			t->key[h] = key;
			t->code[h] = (int16_t)maxCode;
			if (maxCode >= (1 << codeSize)) codeSize++;
			if (maxCode == 4095)
			{
				put_code(o, clear, 12);
				memset(t->key, 0xff, sizeof(t->key));
				codeSize = minCode + 1;
				maxCode = eoi;
			}
			cur = k;
		}
	}
	if (cur >= 0) put_code(o, cur, codeSize);
	put_code(o, eoi, codeSize);
	flush_bits(o);
}

static void le16(uint8_t* p, int v) { p[0] = v & 255; p[1] = (v >> 8) & 255; }

const char* gif_export(const Doc* d, int tag, int scale, const char* path)
{
	if (scale < 1) scale = 1;
	int W = d->w * scale, H = d->h * scale;
	if (W > GIF_MAX_DIM || H > GIF_MAX_DIM) return "GIF too big";
	int seq[ANIM_MAX_SEQ];
	int n = anim_sequence(d, tag, seq);
	int from, to;
	anim_range(d, tag, &from, &to);

	size_t px = (size_t)d->w * d->h;
	GifPalette* p = calloc(1, sizeof(GifPalette));
	uint32_t* rgba = malloc(px * 4);
	uint8_t* idx = malloc(px);
	LzwTable* t = malloc(sizeof(LzwTable));
	if (p) { p->map.keys = calloc(CMAP_SIZE, 4); p->map.vals = malloc(CMAP_SIZE); }
	if (!p || !rgba || !idx || !t || !p->map.keys || !p->map.vals)
	{
		if (p) { free(p->map.keys); free(p->map.vals); }
		free(p); free(rgba); free(idx); free(t);
		return "Out of memory";
	}

	// Pass 1: collect the exact colors. Over 255: fall back to the document's color table.
	p->count = 1;
	bool over = false;
	for (int f = from; f <= to && !over; f++)
	{
		doc_composite(d, f, rgba);
		for (size_t i = 0; i < px && !over; i++)
			if (pal_index(p, rgba[i]) < 0) over = true;
	}
	if (over)
	{
		memset(p->map.keys, 0, CMAP_SIZE * 4);
		p->map.used = 0;
		p->count = 1;
		for (int c = 1; c <= d->colorCount && p->count < 256; c++) pal_index(p, d->colors[c] | 255);
		p->nearest = true;
	}
	int bitsz = 1;
	while ((1 << bitsz) < p->count) bitsz++;

	const char* err = NULL;
	FILE* f = fopen(path, "wb");
	if (!f) err = "Can't open file";
	else
	{
		BitOut o = { .f = f, .ok = true };
		uint8_t hdr[13] = { 'G', 'I', 'F', '8', '9', 'a' };
		le16(hdr + 6, W);
		le16(hdr + 8, H);
		hdr[10] = 0x80 | 0x70 | (bitsz - 1);
		hdr[11] = 0;
		hdr[12] = 0;
		o.ok = fwrite(hdr, 1, 13, f) == 13 && fwrite(p->pal, 1, 3 << bitsz, f) == (size_t)(3 << bitsz);
		static const uint8_t loop[19] = { 0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0', 3, 1, 0, 0, 0 };
		o.ok = o.ok && fwrite(loop, 1, 19, f) == 19;
		int minCode = bitsz < 2 ? 2 : bitsz;
		for (int i = 0; i < n && o.ok; i++)
		{
			int fr = seq[i];
			doc_composite(d, fr, rgba);
			for (size_t k = 0; k < px; k++) idx[k] = (uint8_t)pal_index(p, rgba[k]);
			int cs = (doc_frame_ms(d, fr) + 5) / 10;
			if (cs < 2) cs = 2;
			uint8_t gce[8] = { 0x21, 0xF9, 4, (2 << 2) | 1, 0, 0, 0, 0 };
			le16(gce + 4, cs);
			uint8_t desc[10] = { 0x2C, 0, 0, 0, 0 };
			le16(desc + 5, W);
			le16(desc + 7, H);
			desc[9] = 0;
			o.ok = o.ok && fwrite(gce, 1, 8, f) == 8 && fwrite(desc, 1, 10, f) == 10;
			lzw_frame(&o, t, idx, d->w, d->h, scale, minCode);
		}
		uint8_t end = 0x3B;
		o.ok = o.ok && fwrite(&end, 1, 1, f) == 1;
		o.ok = (fclose(f) == 0) && o.ok;
		if (!o.ok) { remove(path); err = "Write failed"; }
	}
	free(p->map.keys); free(p->map.vals);
	free(p); free(rgba); free(idx); free(t);
	return err;
}
