// SPDX-License-Identifier: MIT
#include "stamps.h"
#include "pxd.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char* stamp_save(const char* dir, const char* name, const uint8_t* px, int w, int h,
                       const Doc* src)
{
	Doc s = {0};
	s.w = w;
	s.h = h;
	s.fps = 8;
	s.frameCount = 1;
	s.layerCount = 1;
	s.layers[0].id = 1;
	s.layers[0].folder = -1;
	s.layers[0].visible = true;
	s.layers[0].opacity = 255;
	snprintf(s.layers[0].name, sizeof(s.layers[0].name), "%s", "Stamp");
	memcpy(s.swatches, src->swatches, sizeof(s.swatches));
	memcpy(s.colors, src->colors, sizeof(s.colors));
	s.colorCount = src->colorCount;
	s.layers[0].cels[0] = (uint8_t*)px;
	char path[256];
	snprintf(path, sizeof(path), "%s/%s.pxd", dir, name);
	return pxd_save(&s, path);
}

const char* stamp_load(const char* dir, const char* name, Doc* target,
                       uint8_t** px, int* w, int* h)
{
	char path[256];
	snprintf(path, sizeof(path), "%s/%s.pxd", dir, name);
	Doc s = {0};
	const char* err = pxd_load(&s, path);
	if (err) return err;
	size_t n = (size_t)s.w * s.h;
	const uint8_t* src = s.layers[s.curLayer].cels[0];
	bool used[DOC_MAX_COLORS + 1] = { false };
	int usedCount = 0;
	for (size_t i = 0; i < n; i++)
		if (src[i] && !used[src[i]]) { used[src[i]] = true; usedCount++; }
	if (target->colorCount + usedCount > DOC_MAX_COLORS)
		doc_compact(target);
	uint8_t map[DOC_MAX_COLORS + 1] = { 0 };
	for (int v = 1; v <= s.colorCount; v++)
		if (used[v])
			map[v] = doc_color_index(target, s.colors[v], false);
	uint8_t* out = malloc(n);
	if (!out) { doc_free(&s); return "Out of memory"; }
	for (size_t i = 0; i < n; i++)
		out[i] = map[src[i]];
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
