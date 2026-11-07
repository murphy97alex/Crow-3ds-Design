// 3DS Pixel UI Designer — Milestone 0
// Top screen: app name. Bottom screen: draw black pixels with the stylus.
// SPDX-License-Identifier: MIT

#include <3ds.h>
#include <citro2d.h>
#include <stdlib.h>
#include <string.h>

#define APP_NAME "Pixel UI Designer"

#define BOTTOM_W 320
#define BOTTOM_H 240
#define TEX_W 512 // power-of-two texture that holds the 320x240 canvas
#define TEX_H 256

#define COLOR_WHITE 0xFFFFFFFFu // RGBA8: 0xRRGGBBAA
#define COLOR_BLACK 0x000000FFu

static C3D_Tex canvasTex;
static Tex3DS_SubTexture canvasSub;
static C2D_Image canvasImg;
static bool canvasDirty = false;

// Offset of pixel (x, y) inside a tiled (8x8 Morton order) GPU texture.
static inline u32 tiledOffset(u32 x, u32 y, u32 w)
{
	u32 tile = ((y >> 3) * (w >> 3) + (x >> 3)) << 6;
	u32 morton = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) |
	             ((x & 4) << 2) | ((y & 4) << 3);
	return tile + morton;
}

static void canvasPlot(int x, int y, u32 color)
{
	if (x < 0 || y < 0 || x >= BOTTOM_W || y >= BOTTOM_H)
		return;
	((u32*)canvasTex.data)[tiledOffset(x, y, TEX_W)] = color;
	canvasDirty = true;
}

// Bresenham line so fast strokes don't leave gaps.
static void canvasLine(int x0, int y0, int x1, int y1, u32 color)
{
	int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (;;)
	{
		canvasPlot(x0, y0, color);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

static void canvasClear(u32 color)
{
	u32* px = (u32*)canvasTex.data;
	for (u32 i = 0; i < TEX_W * TEX_H; i++)
		px[i] = color;
	canvasDirty = true;
}

static bool canvasInit(void)
{
	if (!C3D_TexInit(&canvasTex, TEX_W, TEX_H, GPU_RGBA8))
		return false;
	C3D_TexSetFilter(&canvasTex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetWrap(&canvasTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

	canvasSub.width = BOTTOM_W;
	canvasSub.height = BOTTOM_H;
	canvasSub.left = 0.0f;
	canvasSub.top = 1.0f;
	canvasSub.right = (float)BOTTOM_W / TEX_W;
	canvasSub.bottom = 1.0f - (float)BOTTOM_H / TEX_H;

	canvasImg.tex = &canvasTex;
	canvasImg.subtex = &canvasSub;

	canvasClear(COLOR_WHITE);
	return true;
}

int main(int argc, char* argv[])
{
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();

	C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	C2D_TextBuf textBuf = C2D_TextBufNew(256);
	C2D_Text titleText, hintText;
	C2D_TextParse(&titleText, textBuf, APP_NAME);
	C2D_TextParse(&hintText, textBuf, "Draw on the bottom screen.\nSTART: exit");
	C2D_TextOptimize(&titleText);
	C2D_TextOptimize(&hintText);

	if (!canvasInit())
		svcBreak(USERBREAK_PANIC);

	u32 bgTop = C2D_Color32(0x20, 0x22, 0x28, 0xFF);
	u32 fgTop = C2D_Color32(0xF0, 0xF0, 0xF0, 0xFF);
	u32 dimTop = C2D_Color32(0xA0, 0xA4, 0xB0, 0xFF);
	u32 bgBottom = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);

	bool penDown = false;
	int lastX = 0, lastY = 0;

	while (aptMainLoop())
	{
		hidScanInput();
		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();
		if (kDown & KEY_START)
			break;

		if (kHeld & KEY_TOUCH)
		{
			touchPosition touch;
			hidTouchRead(&touch);
			int x = touch.px, y = touch.py;
			if (penDown)
				canvasLine(lastX, lastY, x, y, COLOR_BLACK);
			else
				canvasPlot(x, y, COLOR_BLACK);
			lastX = x;
			lastY = y;
			penDown = true;
		}
		else
		{
			penDown = false;
		}

		if (canvasDirty)
		{
			C3D_TexFlush(&canvasTex);
			canvasDirty = false;
		}

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

		C2D_TargetClear(top, bgTop);
		C2D_SceneBegin(top);
		float tw, th;
		C2D_TextGetDimensions(&titleText, 1.2f, 1.2f, &tw, &th);
		C2D_DrawText(&titleText, C2D_WithColor, (400.0f - tw) / 2.0f, 90.0f, 0.5f, 1.2f, 1.2f, fgTop);
		C2D_DrawText(&hintText, C2D_WithColor, 16.0f, 190.0f, 0.5f, 0.55f, 0.55f, dimTop);

		C2D_TargetClear(bottom, bgBottom);
		C2D_SceneBegin(bottom);
		C2D_DrawImageAt(canvasImg, 0.0f, 0.0f, 0.5f, NULL, 1.0f, 1.0f);

		C3D_FrameEnd(0);
	}

	C3D_TexDelete(&canvasTex);
	C2D_TextBufDelete(textBuf);
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
