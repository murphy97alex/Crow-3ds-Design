// Color helpers. Palette/texture colors are 0xRRGGBBAA; citro2d colors are ABGR.
// SPDX-License-Identifier: MIT
#pragma once
#include <math.h>
#include <stdint.h>

static inline uint32_t rgba_to_c2d(uint32_t c)
{
	return ((c >> 24) & 0xFF) | (((c >> 16) & 0xFF) << 8) | (((c >> 8) & 0xFF) << 16) | ((c & 0xFF) << 24);
}

static inline uint32_t rgba_make(int r, int g, int b) { return ((uint32_t)r << 24) | (g << 16) | (b << 8) | 0xFF; }

// h in [0,360), s,v in [0,1]
static inline uint32_t hsv_to_rgba(float h, float s, float v)
{
	float c = v * s, hp = fmodf(h, 360.0f) / 60.0f;
	float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
	float r = 0, g = 0, b = 0;
	if (hp < 1)      { r = c; g = x; }
	else if (hp < 2) { r = x; g = c; }
	else if (hp < 3) { g = c; b = x; }
	else if (hp < 4) { g = x; b = c; }
	else if (hp < 5) { r = x; b = c; }
	else             { r = c; b = x; }
	float m = v - c;
	return rgba_make((int)lroundf((r + m) * 255), (int)lroundf((g + m) * 255), (int)lroundf((b + m) * 255));
}

static inline void rgba_to_hsv(uint32_t c, float* h, float* s, float* v)
{
	float r = ((c >> 24) & 0xFF) / 255.0f, g = ((c >> 16) & 0xFF) / 255.0f, b = ((c >> 8) & 0xFF) / 255.0f;
	float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
	float hh = 0;
	if (d > 0)
	{
		if (mx == r)      hh = 60.0f * fmodf((g - b) / d, 6.0f);
		else if (mx == g) hh = 60.0f * ((b - r) / d + 2.0f);
		else              hh = 60.0f * ((r - g) / d + 4.0f);
	}
	if (hh < 0) hh += 360.0f;
	*h = hh;
	*s = mx > 0 ? d / mx : 0;
	*v = mx;
}
