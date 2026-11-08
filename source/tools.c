// SPDX-License-Identifier: MIT
#include "tools.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static Doc* doc;
static Mirror mirror;
static bool changed;
static uint8_t orig[DOC_MAX_W * DOC_MAX_H];
static uint8_t mask[DOC_MAX_W * DOC_MAX_H], mask2[DOC_MAX_W * DOC_MAX_H];
static const uint8_t* valueMap;

void tools_set_value_map(const uint8_t* map) { valueMap = map; }

bool tools_begin(Doc* d, Mirror m)
{
	doc = d;
	mirror = m;
	changed = false;
	memcpy(orig, doc_pixels(d), (size_t)d->w * d->h);
	return true;
}

void tools_restore(void)
{
	memcpy(doc_pixels(doc), orig, (size_t)doc->w * doc->h);
	changed = false;
}

bool tools_changed(void)
{
	return changed || memcmp(doc_pixels(doc), orig, (size_t)doc->w * doc->h) != 0;
}

static void set1(int x, int y, uint8_t v)
{
	if (x < 0 || y < 0 || x >= doc->w || y >= doc->h)
		return;
	if (valueMap)
		v = valueMap[orig[y * doc->w + x]];
	uint8_t* p = &doc_pixels(doc)[y * doc->w + x];
	if (*p != v)
	{
		*p = v;
		changed = true;
	}
}

// Set a pixel plus its mirrored copies.
static void put(int x, int y, uint8_t v)
{
	set1(x, y, v);
	int mx = doc->w - 1 - x, my = doc->h - 1 - y;
	if (mirror == MIRROR_X || mirror == MIRROR_XY) set1(mx, y, v);
	if (mirror == MIRROR_Y || mirror == MIRROR_XY) set1(x, my, v);
	if (mirror == MIRROR_XY) set1(mx, my, v);
}

static uint8_t orig_at(int x, int y) { return orig[y * doc->w + x]; }

static void put_orig(int x, int y)
{
	if (x < 0 || y < 0 || x >= doc->w || y >= doc->h)
		return;
	int mx = doc->w - 1 - x, my = doc->h - 1 - y;
	set1(x, y, orig_at(x, y));
	if (mirror == MIRROR_X || mirror == MIRROR_XY) set1(mx, y, orig_at(mx, y));
	if (mirror == MIRROR_Y || mirror == MIRROR_XY) set1(x, my, orig_at(x, my));
	if (mirror == MIRROR_XY) set1(mx, my, orig_at(mx, my));
}

static bool in_round(int i, int j, int n)
{
	if (n <= 2)
		return true;
	float c = (n - 1) / 2.0f, r = n / 2.0f;
	float dx = i - c, dy = j - c;
	return dx * dx + dy * dy <= r * r - 0.5f;
}

static void stamp(int x, int y, uint8_t v, const Brush* b)
{
	int n = b->size < 1 ? 1 : b->size > BRUSH_MAX ? BRUSH_MAX : b->size;
	int off = (n - 1) / 2;
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++)
		{
			int px = x - off + i, py = y - off + j;
			if (b->shape == BRUSH_ROUND && !in_round(i, j, n))
				continue;
			if (b->shape == BRUSH_DITHER && (((px + py) & 1) != 0 || px < 0 || py < 0))
				continue;
			put(px, py, v);
		}
}

// Bresenham, calling fn for every point including both ends.
typedef void (*PointFn)(int x, int y, uint8_t v, const Brush* b);
static void walk(int x0, int y0, int x1, int y1, uint8_t v, const Brush* b, PointFn fn)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (;;)
	{
		fn(x0, y0, v, b);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

// ---- freehand with pixel-perfect -------------------------------------------------

static int ppAx, ppAy, ppBx, ppBy, ppCount; // last two points of the stroke
static int lastX, lastY;

static bool use_pp(const Brush* b) { return b->pixelPerfect && b->size <= 1 && b->shape != BRUSH_DITHER; }

static void pp_add(int x, int y, uint8_t v, const Brush* b)
{
	(void)b;
	if (ppCount > 0 && x == ppBx && y == ppBy)
		return;
	put(x, y, v);
	if (ppCount >= 2 && abs(ppAx - x) == 1 && abs(ppAy - y) == 1 &&
	    ((ppBx == ppAx && ppBy == y) || (ppBx == x && ppBy == ppAy)))
	{
		// A-B-C forms an L: drop the corner pixel B
		put_orig(ppBx, ppBy);
		ppBx = x;
		ppBy = y;
		return;
	}
	ppAx = ppBx; ppAy = ppBy;
	ppBx = x; ppBy = y;
	if (ppCount < 2) ppCount++;
}

void tools_stroke_start(int x, int y, uint8_t v, const Brush* b)
{
	ppCount = 0;
	lastX = x;
	lastY = y;
	if (use_pp(b))
		pp_add(x, y, v, b);
	else
		stamp(x, y, v, b);
}

void tools_stroke_to(int x, int y, uint8_t v, const Brush* b)
{
	walk(lastX, lastY, x, y, v, b, use_pp(b) ? pp_add : stamp);
	lastX = x;
	lastY = y;
}

// ---- shapes ----------------------------------------------------------------------

void tools_line(int x0, int y0, int x1, int y1, uint8_t v, const Brush* b)
{
	walk(x0, y0, x1, y1, v, b, stamp);
}

static void order(int* a, int* b)
{
	if (*a > *b) { int t = *a; *a = *b; *b = t; }
}

// Draw a mask (bounding box x0,y0 w,h) either filled or as an outline `size` pixels thick.
static void draw_mask(int x0, int y0, int w, int h, uint8_t v, int size, bool filled)
{
	if (!filled)
	{
		// erode `size` times; outline = mask minus eroded mask
		memcpy(mask2, mask, (size_t)w * h);
		for (int s = 0; s < size; s++)
		{
			static uint8_t tmp[DOC_MAX_W * DOC_MAX_H];
			for (int j = 0; j < h; j++)
				for (int i = 0; i < w; i++)
				{
					int k = j * w + i;
					tmp[k] = mask2[k] && i > 0 && j > 0 && i < w - 1 && j < h - 1 &&
					         mask2[k - 1] && mask2[k + 1] && mask2[k - w] && mask2[k + w];
				}
			memcpy(mask2, tmp, (size_t)w * h);
		}
	}
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
		{
			int k = j * w + i;
			if (mask[k] && (filled || !mask2[k]))
				put(x0 + i, y0 + j, v);
		}
}

static bool clamp_box(int* x0, int* y0, int* x1, int* y1)
{
	order(x0, x1);
	order(y0, y1);
	// shapes may extend past the canvas; keep the mask within limits
	return (*x1 - *x0 + 1) <= DOC_MAX_W * 2 && (*y1 - *y0 + 1) <= DOC_MAX_H * 2;
}

void tools_rect(int x0, int y0, int x1, int y1, uint8_t v, int size, bool filled)
{
	order(&x0, &x1);
	order(&y0, &y1);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
		{
			bool edge = x - x0 < size || x1 - x < size || y - y0 < size || y1 - y < size;
			if (filled || edge)
				put(x, y, v);
		}
}

void tools_ellipse(int x0, int y0, int x1, int y1, uint8_t v, int size, bool filled)
{
	if (!clamp_box(&x0, &y0, &x1, &y1))
		return;
	int w = x1 - x0 + 1, h = y1 - y0 + 1;
	if ((long)w * h > (long)sizeof(mask))
		return;
	float rx = w / 2.0f, ry = h / 2.0f;
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
		{
			float dx = (i + 0.5f - rx) / rx, dy = (j + 0.5f - ry) / ry;
			mask[j * w + i] = dx * dx + dy * dy <= 1.0f;
		}
	draw_mask(x0, y0, w, h, v, size, filled);
}

void tools_fill(int x, int y, uint8_t v, bool replaceAll)
{
	if (x < 0 || y < 0 || x >= doc->w || y >= doc->h)
		return;
	uint8_t* px = doc_pixels(doc);
	int w = doc->w, h = doc->h;
	uint8_t target = px[y * w + x];

	// work out which pixels to fill in `mask`, then paint them (with mirroring)
	memset(mask, 0, (size_t)w * h);
	if (replaceAll)
	{
		for (int i = 0; i < w * h; i++)
			mask[i] = px[i] == target;
	}
	else
	{
		static int stack[DOC_MAX_W * DOC_MAX_H];
		int sp = 0;
		stack[sp++] = y * w + x;
		mask[y * w + x] = 1;
		while (sp > 0)
		{
			int k = stack[--sp], kx = k % w, ky = k / w;
			int nb[4] = { kx > 0 ? k - 1 : -1, kx < w - 1 ? k + 1 : -1, ky > 0 ? k - w : -1, ky < h - 1 ? k + w : -1 };
			for (int n = 0; n < 4; n++)
				if (nb[n] >= 0 && !mask[nb[n]] && px[nb[n]] == target)
				{
					mask[nb[n]] = 1;
					stack[sp++] = nb[n];
				}
		}
	}
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			if (mask[j * w + i])
				put(i, j, v);
}

// Connected area of the original pixels around (x, y) into `mask`. Returns false if outside.
static bool area_mask(int x, int y)
{
	int w = doc->w, h = doc->h;
	if (x < 0 || y < 0 || x >= w || y >= h) return false;
	static int stack[DOC_MAX_W * DOC_MAX_H];
	uint8_t target = orig[y * w + x];
	memset(mask, 0, (size_t)w * h);
	int sp = 0;
	stack[sp++] = y * w + x;
	mask[y * w + x] = 1;
	while (sp > 0)
	{
		int k = stack[--sp], kx = k % w, ky = k / w;
		int nb[4] = { kx > 0 ? k - 1 : -1, kx < w - 1 ? k + 1 : -1, ky > 0 ? k - w : -1, ky < h - 1 ? k + w : -1 };
		for (int n = 0; n < 4; n++)
			if (nb[n] >= 0 && !mask[nb[n]] && orig[nb[n]] == target)
			{
				mask[nb[n]] = 1;
				stack[sp++] = nb[n];
			}
	}
	return true;
}

void tools_gradient_fill(int x0, int y0, int x1, int y1, uint8_t v)
{
	static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
	if (!area_mask(x0, y0)) return;
	int w = doc->w, h = doc->h;
	float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			if (!mask[y * w + x]) continue;
			float t = len2 > 0 ? ((x - x0) * dx + (y - y0) * dy) / len2 : 0;
			t = t < 0 ? 0 : t > 1 ? 1 : t;
			// t = 0: all new color; t = 1: all original color
			if (t * 16 < bayer[y & 3][x & 3] + 0.5f)
				put(x, y, v);
		}
}

void tools_snap_line(int x0, int y0, int* x1, int* y1)
{
	static const int dirs[][2] = { {1, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 1} };
	int dx = *x1 - x0, dy = *y1 - y0;
	if (dx == 0 && dy == 0)
		return;
	int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
	float ax = fabsf((float)dx), ay = fabsf((float)dy), len = sqrtf(ax * ax + ay * ay);
	int best = 0;
	float bestCos = -2;
	for (int i = 0; i < 5; i++)
	{
		float vx = dirs[i][0], vy = dirs[i][1], vl = sqrtf(vx * vx + vy * vy);
		float c = (ax * vx + ay * vy) / (len * vl);
		if (c > bestCos) { bestCos = c; best = i; }
	}
	float vx = dirs[best][0], vy = dirs[best][1];
	int k = (int)lroundf((ax * vx + ay * vy) / (vx * vx + vy * vy));
	*x1 = x0 + sx * (int)vx * k;
	*y1 = y0 + sy * (int)vy * k;
}

void tools_snap_square(int x0, int y0, int* x1, int* y1)
{
	int dx = *x1 - x0, dy = *y1 - y0;
	int m = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
	*x1 = x0 + (dx < 0 ? -m : m);
	*y1 = y0 + (dy < 0 ? -m : m);
}
