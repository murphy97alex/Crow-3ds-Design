// SPDX-License-Identifier: MIT
#include "stamps.h"
#include "pxd.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char* stamp_save(const char* dir, const char* name, const uint8_t* px, int w, int h,
                       const uint32_t* palette)
{
	Doc s = {0};
	s.w = w;
	s.h = h;
	s.fps = 8;
	s.frameCount = 1;
	memcpy(s.palette, palette, sizeof(s.palette));
	s.frames[0] = (uint8_t*)px;
	char path[256];
	snprintf(path, sizeof(path), "%s/%s.pxd", dir, name);
	return pxd_save(&s, path);
}

static int nearest(uint32_t c, const uint32_t* pal)
{
	int best = 0;
	long bestD = -1;
	for (int i = 0; i < DOC_PALETTE_SIZE; i++)
	{
		long dr = (long)((c >> 24) & 255) - ((pal[i] >> 24) & 255);
		long dg = (long)((c >> 16) & 255) - ((pal[i] >> 16) & 255);
		long db = (long)((c >> 8) & 255) - ((pal[i] >> 8) & 255);
		long dist = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (bestD < 0 || dist < bestD) { bestD = dist; best = i; }
	}
	return best;
}

const char* stamp_load(const char* dir, const char* name, const uint32_t* palette,
                       uint8_t** px, int* w, int* h)
{
	char path[256];
	snprintf(path, sizeof(path), "%s/%s.pxd", dir, name);
	Doc s = {0};
	const char* err = pxd_load(&s, path);
	if (err) return err;
	uint8_t map[DOC_PALETTE_SIZE + 1];
	map[PIX_TRANSPARENT] = PIX_TRANSPARENT;
	for (int i = 0; i < DOC_PALETTE_SIZE; i++)
		map[i + 1] = nearest(s.palette[i], palette) + 1;
	size_t n = (size_t)s.w * s.h;
	uint8_t* out = malloc(n);
	if (!out) { doc_free(&s); return "Out of memory"; }
	for (size_t i = 0; i < n; i++)
		out[i] = map[s.frames[0][i]];
	*px = out;
	*w = s.w;
	*h = s.h;
	doc_free(&s);
	return NULL;
}

static int cmp(const void* a, const void* b) { return strcasecmp(a, b); }

int stamp_list(const char* dir, char names[][DOC_NAME_MAX], int max)
{
	int n = 0;
	DIR* d = opendir(dir);
	if (!d) return 0;
	struct dirent* e;
	while ((e = readdir(d)) && n < max)
	{
		size_t len = strlen(e->d_name);
		if (len > 4 && len - 4 < DOC_NAME_MAX && !strcasecmp(e->d_name + len - 4, ".pxd"))
		{
			memcpy(names[n], e->d_name, len - 4);
			names[n][len - 4] = '\0';
			n++;
		}
	}
	closedir(d);
	qsort(names, n, DOC_NAME_MAX, cmp);
	return n;
}
