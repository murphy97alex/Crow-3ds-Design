// SPDX-License-Identifier: MIT
#include "text.h"
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

static int glyph_index(char c)
{
	return (c < 32 || c > 126) ? '?' - 32 : c - 32;
}

static int line_width(const PixFont* f, const char* s, int spacing)
{
	int w = 0, n = 0;
	for (; *s && *s != '\n'; s++, n++)
		w += f->advance[glyph_index(*s)] + spacing;
	return n ? w - spacing : 0;
}

uint8_t* text_render(const PixFont* f, const char* s, int scale, int spacing, uint8_t fg, uint8_t outline,
                     int* outW, int* outH)
{
	if (scale < 1) scale = 1;
	// measure in font pixels
	int lines = 1, maxW = 0;
	for (const char* p = s; ; )
	{
		int lw = line_width(f, p, spacing);
		if (lw > maxW) maxW = lw;
		const char* nl = strchr(p, '\n');
		if (!nl) break;
		p = nl + 1;
		lines++;
	}
	int lineH = f->height + 1;
	int tw = maxW, th = lines * lineH - 1;
	if (tw <= 0 || th <= 0) return NULL;
	int pad = outline ? 1 : 0;
	int w = tw * scale + 2 * pad, h = th * scale + 2 * pad;
	uint8_t* px = calloc((size_t)w * h, 1);
	if (!px) return NULL;

	int y = 0;
	for (const char* p = s; ; y += lineH)
	{
		int x = 0;
		for (; *p && *p != '\n'; p++)
		{
			int gi = glyph_index(*p);
			const uint8_t* rows = f->rows + gi * f->height;
			for (int r = 0; r < f->height; r++)
				for (int b = 0; b < 8; b++)
					if (rows[r] >> (7 - b) & 1)
					{
						int fx = x + b, fy = y + r;
						if (fx < 0 || fx >= tw || fy >= th) continue;
						for (int dy = 0; dy < scale; dy++)
							memset(px + (size_t)(pad + fy * scale + dy) * w + pad + fx * scale, fg, scale);
					}
			x += f->advance[gi] + spacing;
		}
		if (*p != '\n') break;
		p++;
	}
	// trim empty columns on the right (the last character's built-in gap)
	int used = 0;
	for (int j = 0; j < h; j++)
		for (int i = w - 1; i >= used; i--)
			if (px[(size_t)j * w + i]) { used = i + 1; break; }
	used += pad;
	if (used < w && used > 0)
	{
		for (int j = 1; j < h; j++)
			memmove(px + (size_t)j * used, px + (size_t)j * w, used);
		w = used;
	}
	if (outline)
	{
		uint8_t* o = malloc((size_t)w * h);
		if (!o) { free(px); return NULL; }
		memcpy(o, px, (size_t)w * h);
		for (int j = 0; j < h; j++)
			for (int i = 0; i < w; i++)
			{
				if (px[j * w + i]) continue;
				bool near = false;
				for (int dy = -1; dy <= 1 && !near; dy++)
					for (int dx = -1; dx <= 1 && !near; dx++)
					{
						int x2 = i + dx, y2 = j + dy;
						near = x2 >= 0 && y2 >= 0 && x2 < w && y2 < h && px[y2 * w + x2] == fg;
					}
				if (near) o[j * w + i] = outline;
			}
		free(px);
		px = o;
	}
	*outW = w;
	*outH = h;
	return px;
}
