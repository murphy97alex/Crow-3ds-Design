// SPDX-License-Identifier: MIT
#include "select.h"
#include <stdlib.h>
#include <string.h>

static bool active, floating;
static Rect sel;                    // selection rect, or the floating layer's rect
static uint8_t base[DOC_MAX_W * DOC_MAX_H]; // frame under the floating layer
static uint8_t* flt;                // floating pixels (sel.w * sel.h)
static uint8_t* clip;
static Mirror floatMirror = MIRROR_OFF;

void sel_set_float_mirror(Mirror m) { floatMirror = m; }
static int clipW, clipH;
static uint8_t* mask;                // sel.w * sel.h, NULL = whole rectangle (not floating)
static bool keepMask;                // re-derive the mask from the pixels after a move

bool sel_active(void) { return active; }
bool sel_floating(void) { return floating; }
Rect sel_rect(void) { return sel; }
bool sel_has_mask(void) { return mask != NULL; }
bool sel_mask_at(int i, int j)
{
	if (i < 0 || j < 0 || i >= sel.w || j >= sel.h) return false;
	if (floating && keepMask && flt) return flt[j * sel.w + i] != PIX_TRANSPARENT;
	return !mask || mask[j * sel.w + i];
}

bool sel_contains(int x, int y)
{
	if (!active || x < sel.x || y < sel.y || x >= sel.x + sel.w || y >= sel.y + sel.h)
		return false;
	return sel_mask_at(x - sel.x, y - sel.y);
}

static void drop_mask(void)
{
	free(mask);
	mask = NULL;
	keepMask = false;
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
	drop_mask();
	sel = (Rect){ x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
	active = true;
}

void sel_all(Doc* d) { sel_set(d, 0, 0, d->w - 1, d->h - 1); }

void sel_clear(Doc* d)
{
	if (floating)
		sel_commit(d);
	drop_mask();
	active = false;
}

void sel_reset(void)
{
	if (floating) hist_abort();
	floating = active = false;
	free(flt);
	flt = NULL;
	drop_mask();
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
			if (v == PIX_TRANSPARENT)
				continue;
			if (x >= 0 && x < d->w)
				px[y * d->w + x] = v;
			int mx = d->w - 1 - x, my = d->h - 1 - y;
			bool mxOk = mx >= 0 && mx < d->w;
			if ((floatMirror == MIRROR_X || floatMirror == MIRROR_XY) && mxOk)
				px[y * d->w + mx] = v;
			if ((floatMirror == MIRROR_Y || floatMirror == MIRROR_XY) && x >= 0 && x < d->w)
				px[my * d->w + x] = v;
			if (floatMirror == MIRROR_XY && mxOk)
				px[my * d->w + mx] = v;
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
		for (int i = 0; i < sel.w; i++)
			p[j * sel.w + i] = sel_mask_at(i, j) ? px[(sel.y + j) * d->w + sel.x + i] : PIX_TRANSPARENT;
	bool hadMask = mask != NULL;
	uint8_t* m = mask;
	mask = NULL;
	float_start(d, p, sel);
	for (int j = 0; j < sel.h; j++)               // hole where the pixels were
		for (int i = 0; i < sel.w; i++)
			if (!m || m[j * sel.w + i])
				base[(sel.y + j) * d->w + sel.x + i] = PIX_TRANSPARENT;
	free(m);
	keepMask = hadMask;
	render(d);
}

void sel_float_pixels(Doc* d, const uint8_t* src, int w, int h, int x, int y)
{
	if (floating)
		sel_commit(d);
	uint8_t* p = malloc((size_t)w * h);
	if (!p) return;
	memcpy(p, src, (size_t)w * h);
	drop_mask();
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
	floatMirror = MIRROR_OFF;
	// keep the selection on the visible part of the canvas
	int x0 = clampi(sel.x, 0, d->w - 1), y0 = clampi(sel.y, 0, d->h - 1);
	int x1 = clampi(sel.x + sel.w - 1, 0, d->w - 1), y1 = clampi(sel.y + sel.h - 1, 0, d->h - 1);
	if (sel.x + sel.w <= 0 || sel.y + sel.h <= 0 || sel.x >= d->w || sel.y >= d->h)
		active = false;
	else
	{
		Rect old = sel;
		Rect r = { x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
		uint8_t* m = NULL;
		if (keepMask && flt && (m = malloc((size_t)r.w * r.h)))
			for (int j = 0; j < r.h; j++)
				for (int i = 0; i < r.w; i++)
					m[j * r.w + i] = flt[(r.y - old.y + j) * old.w + (r.x - old.x + i)] != PIX_TRANSPARENT;
		drop_mask();
		mask = m;
		sel = r;
	}
	free(flt);
	flt = NULL;
	keepMask = false;
}


void sel_cancel(Doc* d)
{
	if (!floating) return;
	memcpy(doc_pixels(d), before, (size_t)d->w * d->h);
	hist_abort();
	floating = false;
	floatMirror = MIRROR_OFF;
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
			for (int i = 0; i < sel.w; i++)
				p[j * sel.w + i] = sel_mask_at(i, j) ? px[(sel.y + j) * d->w + sel.x + i] : PIX_TRANSPARENT;
	}
	*out = p;
	*w = sel.w;
	*h = sel.h;
	return true;
}

// ---- shaped selections ---------------------------------------------------------

// Use a full-canvas mask as the selection (cropped to its bounding box).
static void set_from_canvas_mask(Doc* d, const uint8_t* full)
{
	int x0 = d->w, y0 = d->h, x1 = -1, y1 = -1;
	for (int y = 0; y < d->h; y++)
		for (int x = 0; x < d->w; x++)
			if (full[y * d->w + x])
			{
				if (x < x0) x0 = x;
				if (x > x1) x1 = x;
				if (y < y0) y0 = y;
				if (y > y1) y1 = y;
			}
	if (x1 < 0) { sel_clear(d); return; }
	sel_set(d, x0, y0, x1, y1);   // also drops floating pixels and the old mask
	uint8_t* m = malloc((size_t)sel.w * sel.h);
	if (!m) return;
	bool full_rect = true;
	for (int j = 0; j < sel.h; j++)
		for (int i = 0; i < sel.w; i++)
			full_rect &= (m[j * sel.w + i] = full[(y0 + j) * d->w + x0 + i]) != 0;
	if (full_rect) free(m);
	else mask = m;
}

void sel_wand(Doc* d, int x, int y, bool allOfColor)
{
	if (x < 0 || y < 0 || x >= d->w || y >= d->h) return;
	if (floating) sel_commit(d);
	const uint8_t* px = doc_pixels(d);
	int w = d->w, h = d->h;
	uint8_t target = px[y * w + x];
	uint8_t* full = calloc((size_t)w * h, 1);
	if (!full) return;
	if (allOfColor)
		for (int i = 0; i < w * h; i++) full[i] = px[i] == target;
	else
	{
		int* stack = malloc(sizeof(int) * w * h);
		if (!stack) { free(full); return; }
		int sp = 0;
		stack[sp++] = y * w + x;
		full[y * w + x] = 1;
		while (sp > 0)
		{
			int k = stack[--sp], kx = k % w, ky = k / w;
			int nb[4] = { kx > 0 ? k - 1 : -1, kx < w - 1 ? k + 1 : -1, ky > 0 ? k - w : -1, ky < h - 1 ? k + w : -1 };
			for (int q = 0; q < 4; q++)
				if (nb[q] >= 0 && !full[nb[q]] && px[nb[q]] == target)
				{
					full[nb[q]] = 1;
					stack[sp++] = nb[q];
				}
		}
		free(stack);
	}
	set_from_canvas_mask(d, full);
	free(full);
}

void sel_lasso(Doc* d, const int* pts, int n)
{
	if (n < 1) return;
	if (floating) sel_commit(d);
	int w = d->w, h = d->h;
	uint8_t* full = calloc((size_t)w * h, 1);
	if (!full) return;
	// inside test at pixel centers (even-odd rule)
	if (n >= 3)
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				float px = x + 0.5f, py = y + 0.5f;
				bool in = false;
				for (int i = 0, j = n - 1; i < n; j = i++)
				{
					float xi = pts[i * 2] + 0.5f, yi = pts[i * 2 + 1] + 0.5f;
					float xj = pts[j * 2] + 0.5f, yj = pts[j * 2 + 1] + 0.5f;
					if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi)
						in = !in;
				}
				full[y * w + x] = in;
			}
	// plus the path itself, so thin lassos still select what they touch
	for (int i = 0; i < n; i++)
	{
		int x0 = pts[i * 2], y0 = pts[i * 2 + 1], x1 = pts[((i + 1) % n) * 2], y1 = pts[((i + 1) % n) * 2 + 1];
		int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
		for (;;)
		{
			if (x0 >= 0 && y0 >= 0 && x0 < w && y0 < h) full[y0 * w + x0] = 1;
			if (x0 == x1 && y0 == y1) break;
			int e2 = 2 * err;
			if (e2 >= dy) { err += dy; x0 += sx; }
			if (e2 <= dx) { err += dx; y0 += sy; }
		}
	}
	set_from_canvas_mask(d, full);
	free(full);
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
