// SPDX-License-Identifier: MIT
#include "gfx.h"
#include "color.h"
#include <stdarg.h>
#include <stdio.h>

#define CANVAS_TEX_W 512
#define CANVAS_TEX_H 256

static C3D_Tex canvasTex, checkerTex;
static bool canvasDirty;
static C2D_TextBuf textBuf;

// Offset of texel (x, y) in an 8x8-tiled (Morton order) GPU texture of width w.
static inline u32 tiled(u32 x, u32 y, u32 w)
{
	return (((y >> 3) * (w >> 3) + (x >> 3)) << 6) +
	       ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
}

bool gfx_init(void)
{
	if (!C3D_TexInit(&canvasTex, CANVAS_TEX_W, CANVAS_TEX_H, GPU_RGBA8) ||
	    !C3D_TexInit(&checkerTex, 8, 8, GPU_RGBA8))
		return false;
	C3D_TexSetFilter(&canvasTex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetWrap(&canvasTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	C3D_TexSetFilter(&checkerTex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetWrap(&checkerTex, GPU_REPEAT, GPU_REPEAT);

	// 8x8 texture = 2x2 checker cells of 4 texels
	u32* c = (u32*)checkerTex.data;
	for (u32 y = 0; y < 8; y++)
		for (u32 x = 0; x < 8; x++)
			c[tiled(x, y, 8)] = (((x >> 2) ^ (y >> 2)) & 1) ? 0xC8C8C8FF : 0xF4F4F4FF;
	C3D_TexFlush(&checkerTex);

	textBuf = C2D_TextBufNew(4096);
	return textBuf != NULL;
}

void gfx_fini(void)
{
	C2D_TextBufDelete(textBuf);
	C3D_TexDelete(&canvasTex);
	C3D_TexDelete(&checkerTex);
}

void gfx_frame_begin(void) { C2D_TextBufClear(textBuf); }

static inline u32 pix_color(const Doc* d, uint8_t v)
{
	return v == PIX_TRANSPARENT ? 0x00000000 : d->palette[v - 1];
}

void gfx_canvas_set(const Doc* d, int x, int y)
{
	((u32*)canvasTex.data)[tiled(x, y, CANVAS_TEX_W)] = pix_color(d, doc_get(d, x, y));
	canvasDirty = true;
}

void gfx_canvas_rebuild(const Doc* d)
{
	u32* t = (u32*)canvasTex.data;
	for (int y = 0; y < d->h; y++)
		for (int x = 0; x < d->w; x++)
			t[tiled(x, y, CANVAS_TEX_W)] = pix_color(d, doc_get(d, x, y));
	canvasDirty = true;
}

void gfx_canvas_flush(void)
{
	if (canvasDirty)
	{
		C3D_TexFlush(&canvasTex);
		canvasDirty = false;
	}
}

static void draw_tex(C3D_Tex* tex, float x, float y, float w, float h,
                     float u0, float v0, float u1, float v1)
{
	if (w < 1 || h < 1)
		return;
	Tex3DS_SubTexture sub = {
		.width = (u16)w, .height = (u16)h,
		.left = u0, .top = 1.0f - v0, .right = u1, .bottom = 1.0f - v1,
	};
	C2D_Image img = { .tex = tex, .subtex = &sub };
	C2D_DrawImageAt(img, x, y, 0.5f, NULL, w / sub.width, h / sub.height);
}

void gfx_draw_checker(float x, float y, float w, float h, float cell)
{
	if (w <= 0 || h <= 0)
		return;
	// one texture repeat = 2 cells
	draw_tex(&checkerTex, x, y, w, h, 0, 0, w / (2 * cell), h / (2 * cell));
}

void gfx_draw_canvas(const Doc* d, float x, float y, float scale, float clipY0, float clipY1)
{
	float w = d->w * scale, h = d->h * scale;
	// Clip the visible part so the quad never covers UI outside the view.
	float x0 = x < 0 ? 0 : x, y0 = y < clipY0 ? clipY0 : y;
	float x1 = x + w > 400 ? 400 : x + w, y1 = y + h > clipY1 ? clipY1 : y + h;
	if (x1 <= x0 || y1 <= y0)
		return;
	gfx_draw_checker(x0, y0, x1 - x0, y1 - y0, 4);
	float u0 = (x0 - x) / scale / CANVAS_TEX_W, u1 = (x1 - x) / scale / CANVAS_TEX_W;
	float v0 = (y0 - y) / scale / CANVAS_TEX_H, v1 = (y1 - y) / scale / CANVAS_TEX_H;
	draw_tex(&canvasTex, x0, y0, x1 - x0, y1 - y0, u0, v0, u1, v1);
}

void gfx_text(float x, float y, float scale, u32 color, const char* fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	C2D_Text t;
	C2D_TextParse(&t, textBuf, buf);
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

float gfx_text_width(float scale, const char* s)
{
	C2D_Text t;
	C2D_TextParse(&t, textBuf, s);
	float w, h;
	C2D_TextGetDimensions(&t, scale, scale, &w, &h);
	return w;
}

void gfx_rect_outline(float x, float y, float w, float h, float t, u32 color)
{
	C2D_DrawRectSolid(x, y, 0.5f, w, t, color);
	C2D_DrawRectSolid(x, y + h - t, 0.5f, w, t, color);
	C2D_DrawRectSolid(x, y, 0.5f, t, h, color);
	C2D_DrawRectSolid(x + w - t, y, 0.5f, t, h, color);
}
