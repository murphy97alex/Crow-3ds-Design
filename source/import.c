// SPDX-License-Identifier: MIT
#include "import.h"
#include <png.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fit_size(int w, int h, int maxW, int maxH, int* nw, int* nh)
{
	*nw = w;
	*nh = h;
	if (w <= maxW && h <= maxH) return;
	float s = (float)maxW / w;
	if ((float)maxH / h < s) s = (float)maxH / h;
	*nw = (int)(w * s);
	*nh = (int)(h * s);
	if (*nw < 1) *nw = 1;
	if (*nh < 1) *nh = 1;
}

static void sample_row(const uint8_t* src, int w, uint32_t* dst, int nw)
{
	for (int x = 0; x < nw; x++)
	{
		const uint8_t* p = src + (size_t)(x * (long)w / nw) * 4;
		dst[x] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
	}
}

const char* import_read_png(const char* path, int maxW, int maxH, uint32_t** rgba, int* w, int* h, bool* shrunk)
{
	FILE* fp = fopen(path, "rb");
	if (!fp) return "Can't open file";
	uint8_t sig[8];
	if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8)) { fclose(fp); return "Not a PNG"; }

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png ? png_create_info_struct(png) : NULL;
	if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return "Out of memory"; }

	uint8_t* volatile row = NULL;      // one decoded row (or the whole image if interlaced)
	uint32_t* volatile out = NULL;
	const char* volatile err = NULL;
	png_bytep* volatile rows = NULL;
	if (setjmp(png_jmpbuf(png)))
	{
		free(rows);
		png_destroy_read_struct(&png, &info, NULL);
		fclose(fp);
		free(row);
		free(out);
		return err ? err : "Couldn't decode PNG";
	}
	png_init_io(png, fp);
	png_set_sig_bytes(png, 8);
	png_read_info(png, info);
	int sw = png_get_image_width(png, info), sh = png_get_image_height(png, info);
	if (sw < 1 || sh < 1 || sw > IMPORT_MAX_DIM || sh > IMPORT_MAX_DIM)
	{
		err = "Image too large (16384 px max)";
		png_error(png, "size");
	}
	int ct = png_get_color_type(png, info);
	png_set_expand(png);                       // palette/low-bit gray -> 8 bit, tRNS -> alpha
	png_set_strip_16(png);
	if (!(ct & PNG_COLOR_MASK_COLOR)) png_set_gray_to_rgb(png);
	if (!(ct & PNG_COLOR_MASK_ALPHA) && !png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
	int passes = png_set_interlace_handling(png);
	png_read_update_info(png, info);
	size_t rowBytes = png_get_rowbytes(png, info);
	if (rowBytes != (size_t)sw * 4) { err = "Unsupported PNG format"; png_error(png, "format"); }

	int nw, nh;
	fit_size(sw, sh, maxW, maxH, &nw, &nh);
	out = malloc((size_t)nw * nh * 4);
	if (!out) { err = "Out of memory"; png_error(png, "oom"); }

	if (passes == 1)
	{
		row = malloc(rowBytes);
		if (!row) { err = "Out of memory"; png_error(png, "oom"); }
		int yo = 0;
		for (int y = 0; y < sh && yo < nh; y++)
		{
			png_read_row(png, row, NULL);
			while (yo < nh && (int)((long)yo * sh / nh) == y)
				sample_row(row, sw, out + (size_t)yo++ * nw, nw);
		}
	}
	else
	{
		// interlaced PNGs need the whole image in memory
		row = malloc(rowBytes * sh);
		if (!row) { err = "Out of memory (interlaced PNG - save it non-interlaced)"; png_error(png, "oom"); }
		rows = malloc(sizeof(png_bytep) * sh);
		if (!rows) { err = "Out of memory"; png_error(png, "oom"); }
		for (int y = 0; y < sh; y++) rows[y] = row + rowBytes * y;
		png_read_image(png, rows);
		free(rows);
		rows = NULL;
		for (int yo = 0; yo < nh; yo++)
			sample_row(row + rowBytes * (size_t)((long)yo * sh / nh), sw, out + (size_t)yo * nw, nw);
	}
	png_destroy_read_struct(&png, &info, NULL);
	fclose(fp);
	free(row);
	*rgba = out;
	*w = nw;
	*h = nh;
	if (shrunk) *shrunk = nw != sw || nh != sh;
	return NULL;
}

bool import_fit(uint32_t** rgba, int* w, int* h, int maxW, int maxH)
{
	if (*w <= maxW && *h <= maxH)
		return true;
	float s = (float)maxW / *w;
	if ((float)maxH / *h < s) s = (float)maxH / *h;
	int nw = (int)(*w * s), nh = (int)(*h * s);
	if (nw < 1) nw = 1;
	if (nh < 1) nh = 1;
	uint32_t* out = malloc((size_t)nw * nh * 4);
	if (!out) return false;
	for (int y = 0; y < nh; y++)
		for (int x = 0; x < nw; x++)
			out[y * nw + x] = (*rgba)[(size_t)(y * *h / nh) * *w + x * *w / nw];
	free(*rgba);
	*rgba = out;
	*w = nw;
	*h = nh;
	return true;
}

// Small open-addressing set of colors for counting.
typedef struct { uint32_t c; int n; } Bucket;

static int count_colors(const uint32_t* rgba, size_t n, Bucket* tab, int cap)
{
	int used = 0;
	for (size_t i = 0; i < n; i++)
	{
		if ((rgba[i] & 255) < 128) continue;
		uint32_t c = rgba[i] | 0xFF;
		unsigned hsh = (c * 2654435761u) % cap;
		while (tab[hsh].n && tab[hsh].c != c) hsh = (hsh + 1) % cap;
		if (!tab[hsh].n)
		{
			if (used >= cap / 2) return -1;  // too many colors to count exactly
			tab[hsh].c = c;
			used++;
		}
		tab[hsh].n++;
	}
	return used;
}

uint8_t* import_to_indexed(Doc* d, const uint32_t* rgba, int w, int h)
{
	size_t n = (size_t)w * h;
	uint8_t* px = malloc(n);
	if (!px) return NULL;
	if (d->colorCount > DOC_MAX_COLORS / 2)
		doc_compact(d);
	// small cache so photos with many colors stay quick
	uint32_t lastC = 0;
	uint8_t lastV = 0;
	for (size_t i = 0; i < n; i++)
	{
		if ((rgba[i] & 255) < 128) { px[i] = PIX_TRANSPARENT; continue; }
		uint32_t c = rgba[i] | 0xFF;
		if (c != lastC || !lastV) { lastV = doc_color_index(d, c, false); lastC = c; }
		px[i] = lastV;
	}
	return px;
}

static int cmp_bucket(const void* a, const void* b)
{
	return ((const Bucket*)b)->n - ((const Bucket*)a)->n;
}

int import_palette(const uint32_t* rgba, int w, int h, uint32_t* out, int max)
{
	enum { CAP = 8192 };
	Bucket* tab = calloc(CAP, sizeof(Bucket));
	if (!tab) return 0;
	int used = count_colors(rgba, (size_t)w * h, tab, CAP);
	int k = 0;
	if (used > 0)
	{
		for (int i = 0; i < CAP; i++)
			if (tab[i].n) tab[k++] = tab[i];
		qsort(tab, k, sizeof(Bucket), cmp_bucket);
	}
	int n = k < max ? k : max;
	for (int i = 0; i < n; i++)
		out[i] = tab[i].c;
	free(tab);
	return n;
}
