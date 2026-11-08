// SPDX-License-Identifier: MIT
#include "import.h"
#include <png.h>
#include <stdlib.h>
#include <string.h>

const char* import_read_png(const char* path, uint32_t** rgba, int* w, int* h)
{
	png_image img;
	memset(&img, 0, sizeof(img));
	img.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_file(&img, path))
		return "Not a readable PNG";
	if (img.width < 1 || img.height < 1 || img.width > IMPORT_MAX_DIM || img.height > IMPORT_MAX_DIM)
	{
		png_image_free(&img);
		return "Image too large";
	}
	img.format = PNG_FORMAT_RGBA;
	uint8_t* buf = malloc(PNG_IMAGE_SIZE(img));
	if (!buf) { png_image_free(&img); return "Out of memory"; }
	if (!png_image_finish_read(&img, NULL, buf, 0, NULL))
	{
		free(buf);
		return "Couldn't decode PNG";
	}
	size_t n = (size_t)img.width * img.height;
	uint32_t* out = malloc(n * 4);
	if (!out) { free(buf); return "Out of memory"; }
	for (size_t i = 0; i < n; i++)
	{
		const uint8_t* p = buf + i * 4;
		out[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
	}
	free(buf);
	*rgba = out;
	*w = img.width;
	*h = img.height;
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
