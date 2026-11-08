// SPDX-License-Identifier: MIT
#include "select.h"
#include <stdlib.h>
#include <string.h>

static bool active, floating;
static Rect sel;                    // selection rect, or the floating layer's rect
static uint8_t base[DOC_MAX_W * DOC_MAX_H]; // frame under the floating layer
static uint8_t* flt;                // floating pixels (sel.w * sel.h)
static uint8_t* clip;
static int clipW, clipH;

bool sel_active(void) { return active; }
bool sel_floating(void) { return floating; }
Rect sel_rect(void) { return sel; }
bool sel_contains(int x, int y)
{
	return active && x >= sel.x && y >= sel.y && x < sel.x + sel.w && y < sel.y + sel.h;
}

static uint8_t before[DOC_MAX_W * DOC_MAX_H]; // frame before the current lift/paste/shift

// Start an undoable edit: remember the frame for undo and for "did anything change?".
static void edit_begin(Doc* d)
{
	memcpy(before, doc_pixels(d), (size_t)d->w * d->h);
	hist_begin(d);
}

static bool frame_changed(Doc* d) { return memcmp(before, doc_pixels(d), (size_t)d->w * d->h) != 0; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void sel_set(Doc* d, int x0, int y0, int x1, int y1)
{
	if (floating)
		sel_commit(d);
	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
	x0 = clampi(x0, 0, d->w - 1); x1 = clampi(x1, 0, d->w - 1);
	y0 = clampi(y0, 0, d->h - 1); y1 = clampi(y1, 0, d->h - 1);
	sel = (Rect){ x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
	active = true;
}

void sel_all(Doc* d) { sel_set(d, 0, 0, d->w - 1, d->h - 1); }

void sel_clear(Doc* d)
{
	if (floating)
		sel_commit(d);
	active = false;
}

void sel_reset(void)
{
	if (floating) hist_abort();
	floating = active = false;
	free(flt);
	flt = NULL;
	free(clip);   // pixel values only make sense in the document they came from
	clip = NULL;
}

// Composite: frame = base + floating pixels (transparent pixels don't overwrite).
static void render(Doc* d)
{
	uint8_t* px = doc_pixels(d);
	memcpy(px, base, (size_t)d->w * d->h);
	for (int j = 0; j < sel.h; j++)
	{
		int y = sel.y + j;
		if (y < 0 || y >= d->h) continue;
		for (int i = 0; i < sel.w; i++)
		{
			int x = sel.x + i;
			uint8_t v = flt[j * sel.w + i];
			if (x >= 0 && x < d->w && v != PIX_TRANSPARENT)
				px[y * d->w + x] = v;
		}
	}
}

static void float_start(Doc* d, uint8_t* pixels, Rect r)
{
	edit_begin(d);
	memcpy(base, doc_pixels(d), (size_t)d->w * d->h);
	free(flt);
	flt = pixels;
	sel = r;
	active = floating = true;
}

void sel_lift(Doc* d)
{
	if (!active || floating)
		return;
	uint8_t* p = malloc((size_t)sel.w * sel.h);
	if (!p) return;
	uint8_t* px = doc_pixels(d);
	for (int j = 0; j < sel.h; j++)
		memcpy(p + j * sel.w, px + (sel.y + j) * d->w + sel.x, sel.w);
	float_start(d, p, sel);
	for (int j = 0; j < sel.h; j++)               // hole where the pixels were
		memset(base + (sel.y + j) * d->w + sel.x, PIX_TRANSPARENT, sel.w);
	render(d);
}

void sel_float_pixels(Doc* d, const uint8_t* src, int w, int h, int x, int y)
{
	if (floating)
		sel_commit(d);
	uint8_t* p = malloc((size_t)w * h);
	if (!p) return;
	memcpy(p, src, (size_t)w * h);
	float_start(d, p, (Rect){ x, y, w, h });
	render(d);
}

void sel_float_replace(Doc* d, const uint8_t* src, int w, int h, int x, int y)
{
	if (!floating) { sel_float_pixels(d, src, w, h, x, y); return; }
	uint8_t* p = malloc((size_t)w * h);
	if (!p) return;
	memcpy(p, src, (size_t)w * h);
	free(flt);
	flt = p;
	sel = (Rect){ x, y, w, h };
	render(d);
}

void sel_move_to(Doc* d, int x, int y)
{
	if (!floating) return;
	sel.x = x;
	sel.y = y;
	render(d);
}

void sel_commit(Doc* d)
{
	if (!floating) return;
	render(d);
	if (frame_changed(d)) { hist_commit(); d->dirty = true; }
	else hist_abort();
	floating = false;
	free(flt);
	flt = NULL;
	// keep the selection on the visible part of the canvas
	int x0 = clampi(sel.x, 0, d->w - 1), y0 = clampi(sel.y, 0, d->h - 1);
	int x1 = clampi(sel.x + sel.w - 1, 0, d->w - 1), y1 = clampi(sel.y + sel.h - 1, 0, d->h - 1);
	if (sel.x + sel.w <= 0 || sel.y + sel.h <= 0 || sel.x >= d->w || sel.y >= d->h)
		active = false;
	else
		sel = (Rect){ x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
}


void sel_cancel(Doc* d)
{
	if (!floating) return;
	memcpy(doc_pixels(d), before, (size_t)d->w * d->h);
	hist_abort();
	floating = false;
	free(flt);
	flt = NULL;
	active = false;
}


// ---- actions ---------------------------------------------------------------------

static bool ensure_float(Doc* d)
{
	if (!active) sel_all(d);
	if (!floating) sel_lift(d);
	return floating;
}

static void finish(Doc* d, bool wasFloating)
{
	render(d);
	if (!wasFloating) sel_commit(d);
}

void sel_copy(Doc* d)
{
	if (!active) return;
	uint8_t* p;
	int w, h;
	if (!sel_get_pixels(d, &p, &w, &h)) return;
	free(clip);
	clip = p;
	clipW = w;
	clipH = h;
}

bool sel_has_clip(void) { return clip != NULL; }

void sel_cut(Doc* d)
{
	if (!active) return;
	sel_copy(d);
	sel_delete(d);
}

void sel_paste(Doc* d, int x, int y)
{
	if (clip)
		sel_float_pixels(d, clip, clipW, clipH, x, y);
}

void sel_delete(Doc* d)
{
	if (!active) return;
	if (!floating) sel_lift(d);
	if (!floating) return;
	memset(flt, PIX_TRANSPARENT, (size_t)sel.w * sel.h);
	sel_commit(d);
	active = false;
}

void sel_flip(Doc* d, bool horizontal)
{
	bool was = floating;
	if (!ensure_float(d)) return;
	int w = sel.w, h = sel.h;
	for (int j = 0; j < (horizontal ? h : h / 2); j++)
		for (int i = 0; i < (horizontal ? w / 2 : w); i++)
		{
			int a = j * w + i;
			int b = horizontal ? j * w + (w - 1 - i) : (h - 1 - j) * w + i;
			uint8_t t = flt[a]; flt[a] = flt[b]; flt[b] = t;
		}
	finish(d, was);
}

void sel_rotate(Doc* d)
{
	bool was = floating;
	if (!ensure_float(d)) return;
	int w = sel.w, h = sel.h;
	uint8_t* p = malloc((size_t)w * h);
	if (!p) return;
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			p[i * h + (h - 1 - j)] = flt[j * w + i];  // new size h x w
	free(flt);
	flt = p;
	sel.x += (w - h) / 2;
	sel.y += (h - w) / 2;
	sel.w = h;
	sel.h = w;
	finish(d, was);
}

void sel_outline(Doc* d, uint8_t v)
{
	bool was = floating;
	if (!ensure_float(d)) return;
	int w = sel.w + 2, h = sel.h + 2;
	uint8_t* p = calloc((size_t)w * h, 1);
	if (!p) return;
	for (int j = 0; j < sel.h; j++)
		memcpy(p + (j + 1) * w + 1, flt + j * sel.w, sel.w);
	uint8_t* out = malloc((size_t)w * h);
	if (!out) { free(p); return; }
	memcpy(out, p, (size_t)w * h);
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
		{
			if (p[j * w + i] != PIX_TRANSPARENT) continue;
			bool n = (i > 0 && p[j * w + i - 1]) || (i < w - 1 && p[j * w + i + 1]) ||
			         (j > 0 && p[(j - 1) * w + i]) || (j < h - 1 && p[(j + 1) * w + i]);
			if (n) out[j * w + i] = v;
		}
	free(p);
	free(flt);
	flt = out;
	sel = (Rect){ sel.x - 1, sel.y - 1, w, h };
	finish(d, was);
}

bool sel_get_pixels(Doc* d, uint8_t** out, int* w, int* h)
{
	if (!active) return false;
	uint8_t* p = malloc((size_t)sel.w * sel.h);
	if (!p) return false;
	if (floating)
		memcpy(p, flt, (size_t)sel.w * sel.h);
	else
	{
		uint8_t* px = doc_pixels(d);
		for (int j = 0; j < sel.h; j++)
			memcpy(p + j * sel.w, px + (sel.y + j) * d->w + sel.x, sel.w);
	}
	*out = p;
	*w = sel.w;
	*h = sel.h;
	return true;
}

// ---- shift -----------------------------------------------------------------------

void shift_begin(Doc* d)
{
	sel_clear(d);
	edit_begin(d);
	memcpy(base, doc_pixels(d), (size_t)d->w * d->h);
}

void shift_set(Doc* d, int dx, int dy)
{
	int w = d->w, h = d->h;
	dx = ((dx % w) + w) % w;
	dy = ((dy % h) + h) % h;
	uint8_t* px = doc_pixels(d);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			px[((y + dy) % h) * w + (x + dx) % w] = base[y * w + x];
}

void shift_end(Doc* d, bool keep)
{
	if (keep && frame_changed(d)) { hist_commit(); d->dirty = true; }
	else
	{
		memcpy(doc_pixels(d), base, (size_t)d->w * d->h);
		hist_abort();
	}
}
