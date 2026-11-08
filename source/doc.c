// SPDX-License-Identifier: MIT
#include "doc.h"
#include <stdlib.h>
#include <string.h>

// Default palette for new projects. Original to this project and released under CC0 1.0
// (see docs/PALETTE.md). Row 1: grays, skin/browns, reds, oranges/yellows.
// Row 2: greens, teals, blues, purples, pinks.
const uint32_t DOC_DEFAULT_PALETTE[DOC_PALETTE_SIZE] = {
	0x000000FF, 0x2B2C34FF, 0x565966FF, 0x8B8E9BFF, 0xC3C6CFFF, 0xFFFFFFFF,
	0x3E2618FF, 0x7A4A2CFF, 0xC07E50FF, 0xF0C29AFF,
	0x6E1A26FF, 0xC02C34FF, 0xF2604CFF,
	0xE88A24FF, 0xF8C838FF, 0xFFF29AFF,
	0x1C4A2EFF, 0x2E8A3EFF, 0x6CC650FF, 0xC6EE8EFF,
	0x145A66FF, 0x22A6B0FF, 0x7CE8E6FF,
	0x1A2050FF, 0x2A4CB0FF, 0x3E8AF0FF, 0x9CCCFFFF,
	0x3E1E5CFF, 0x7A3EB4FF, 0xC08AF0FF,
	0xB8307EFF, 0xF27AB8FF,
};

bool doc_new(Doc* d, int w, int h)
{
	if (w < 1 || h < 1 || w > DOC_MAX_W || h > DOC_MAX_H)
		return false;
	uint8_t* px = calloc((size_t)w * h, 1);
	if (!px)
		return false;
	doc_free(d);
	d->w = w;
	d->h = h;
	d->fps = 8;
	memcpy(d->swatches, DOC_DEFAULT_PALETTE, sizeof(d->swatches));
	d->colorCount = 0;
	d->frameCount = 1;
	d->curFrame = 0;
	d->frames[0] = px;
	d->name[0] = '\0';
	d->dirty = false;
	hist_clear();
	return true;
}

static long color_dist(uint32_t a, uint32_t b)
{
	long dr = (long)((a >> 24) & 255) - ((b >> 24) & 255);
	long dg = (long)((a >> 16) & 255) - ((b >> 16) & 255);
	long db = (long)((a >> 8) & 255) - ((b >> 8) & 255);
	return dr * dr * 3 + dg * dg * 4 + db * db * 2;
}

void doc_compact(Doc* d)
{
	bool used[DOC_MAX_COLORS + 1] = { false };
	size_t n = (size_t)d->w * d->h;
	for (int f = 0; f < d->frameCount; f++)
		for (size_t i = 0; i < n; i++)
			used[d->frames[f][i]] = true;
	uint8_t remap[DOC_MAX_COLORS + 1] = { 0 };
	int count = 0;
	for (int v = 1; v <= d->colorCount; v++)
		if (used[v])
		{
			d->colors[++count] = d->colors[v];
			remap[v] = count;
		}
	for (int f = 0; f < d->frameCount; f++)
		for (size_t i = 0; i < n; i++)
			d->frames[f][i] = remap[d->frames[f][i]];
	d->colorCount = count;
}

uint8_t doc_color_index(Doc* d, uint32_t rgba, bool allowCompact)
{
	rgba |= 0xFF;  // drawing colors are opaque
	for (int v = 1; v <= d->colorCount; v++)
		if (d->colors[v] == rgba)
			return v;
	if (d->colorCount >= DOC_MAX_COLORS && allowCompact)
		doc_compact(d);
	if (d->colorCount < DOC_MAX_COLORS)
	{
		d->colors[++d->colorCount] = rgba;
		return d->colorCount;
	}
	int best = 1;
	for (int v = 2; v <= d->colorCount; v++)
		if (color_dist(d->colors[v], rgba) < color_dist(d->colors[best], rgba))
			best = v;
	return best;
}

bool doc_recolor(Doc* d, const uint32_t* from, const uint32_t* to, int n)
{
	bool changed = false;
	int count = d->colorCount;
	uint32_t orig[DOC_MAX_COLORS + 1];
	memcpy(orig, d->colors, sizeof(orig));
	for (int v = 1; v <= count; v++)
		for (int i = 0; i < n; i++)
			if (orig[v] == (from[i] | 0xFF) && from[i] != to[i])
			{
				d->colors[v] = to[i] | 0xFF;
				changed = true;
				break;
			}
	if (!changed)
		return false;
	// merge entries that now hold the same color
	uint8_t remap[DOC_MAX_COLORS + 1];
	bool needRemap = false;
	for (int v = 0; v <= count; v++)
	{
		remap[v] = v;
		for (int u = 1; u < v; u++)
			if (remap[u] == u && d->colors[u] == d->colors[v]) { remap[v] = u; needRemap = true; break; }
	}
	if (needRemap)
	{
		size_t np = (size_t)d->w * d->h;
		for (int f = 0; f < d->frameCount; f++)
			for (size_t i = 0; i < np; i++)
				d->frames[f][i] = remap[d->frames[f][i]];
	}
	return true;
}

void doc_free(Doc* d)
{
	for (int i = 0; i < DOC_MAX_FRAMES; i++)
	{
		free(d->frames[i]);
		d->frames[i] = NULL;
	}
	d->frameCount = 0;
}

// ---- history ---------------------------------------------------------------

#define HIST_MAX 48

typedef struct {
	int frame;
	uint32_t colors[DOC_MAX_COLORS + 1];
	int colorCount;
	uint8_t* px;
} Snap;

static Snap undoStack[HIST_MAX], redoStack[HIST_MAX];
static int undoCount, redoCount;

static void snap_free(Snap* s) { free(s->px); s->px = NULL; }

static void stack_clear(Snap* st, int* n)
{
	for (int i = 0; i < *n; i++)
		snap_free(&st[i]);
	*n = 0;
}

void hist_abort(void);

void hist_clear(void)
{
	hist_abort();
	stack_clear(undoStack, &undoCount);
	stack_clear(redoStack, &redoCount);
}

static bool snap_take(Snap* s, const Doc* d)
{
	size_t n = (size_t)d->w * d->h;
	s->px = malloc(n);
	if (!s->px)
		return false;
	memcpy(s->px, d->frames[d->curFrame], n);
	memcpy(s->colors, d->colors, sizeof(s->colors));
	s->colorCount = d->colorCount;
	s->frame = d->curFrame;
	return true;
}

static void snap_restore(Snap* s, Doc* d)
{
	memcpy(d->frames[s->frame], s->px, (size_t)d->w * d->h);
	memcpy(d->colors, s->colors, sizeof(d->colors));
	d->colorCount = s->colorCount;
	d->curFrame = s->frame;
	snap_free(s);
}

static void stack_push(Snap* st, int* n, Snap s)
{
	if (*n == HIST_MAX)
	{
		snap_free(&st[0]);
		memmove(&st[0], &st[1], sizeof(Snap) * (HIST_MAX - 1));
		(*n)--;
	}
	st[(*n)++] = s;
}

void hist_push(const Doc* d)
{
	Snap s;
	if (!snap_take(&s, d))
		return;
	stack_push(undoStack, &undoCount, s);
	stack_clear(redoStack, &redoCount);
}

static Snap pending;
static bool hasPending;

void hist_begin(const Doc* d)
{
	hist_abort();
	hasPending = snap_take(&pending, d);
}

void hist_commit(void)
{
	if (!hasPending)
		return;
	stack_push(undoStack, &undoCount, pending);
	stack_clear(redoStack, &redoCount);
	hasPending = false;
}

void hist_abort(void)
{
	if (hasPending)
		snap_free(&pending);
	hasPending = false;
}

static bool hist_move(Doc* d, Snap* from, int* nFrom, Snap* to, int* nTo)
{
	if (*nFrom == 0)
		return false;
	d->curFrame = from[*nFrom - 1].frame;
	Snap cur;
	if (!snap_take(&cur, d))
		return false;
	Snap s = from[--(*nFrom)];
	snap_restore(&s, d);
	stack_push(to, nTo, cur);
	d->dirty = true;
	return true;
}

bool hist_undo(Doc* d) { return hist_move(d, undoStack, &undoCount, redoStack, &redoCount); }
bool hist_redo(Doc* d) { return hist_move(d, redoStack, &redoCount, undoStack, &undoCount); }
