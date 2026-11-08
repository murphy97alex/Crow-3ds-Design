// SPDX-License-Identifier: MIT
#include "picker.h"
#include "color.h"
#include "gfx.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Layout (bottom screen 320x240)
#define SQ_X 8        // saturation (x) / brightness (y) square
#define SQ_Y 22
#define SQ_W 200
#define SQ_H 150
#define HUE_Y 178     // hue bar
#define HUE_H 16
#define SWATCH_Y 202  // project palette, 2 rows of 16
#define SWATCH_H 15
#define COL_X 216     // right column
#define COL_W 96
#define PREV_Y 22
#define BTN_HEX_Y 100
#define BTN_LIST_Y 126
#define BTN_SMALL_H 22
#define BTN_OK_Y 166
#define BTN_CANCEL_Y 200
#define BTN_H 28

enum { DRAG_NONE, DRAG_SQUARE, DRAG_HUE };

static int slot;
static uint32_t oldColor;
static float hue, sat, val;
static int drag;
static uint32_t pal[32];
static uint32_t listColors[32];
static int listCount;
static const char* listError;

static uint32_t current(void) { return hsv_to_rgba(hue, sat, val); }
uint32_t picker_color(void) { return current(); }

void picker_open(int s, uint32_t rgba, const uint32_t* palette)
{
	memcpy(pal, palette, sizeof(pal));
	listError = NULL;
	slot = s;
	oldColor = rgba;
	rgba_to_hsv(rgba, &hue, &sat, &val);
	drag = DRAG_NONE;
}

static bool in_rect(touchPosition t, int x, int y, int w, int h)
{
	return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

static float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// Keep hue/saturation when a palette color is gray or black.
static void set_rgba(uint32_t c)
{
	float h, s, v;
	rgba_to_hsv(c, &h, &s, &v);
	if (s > 0 && v > 0) hue = h;
	if (v > 0) sat = s;
	val = v;
}

static void ask_hex(void)
{
	uint32_t c = current();
	char init[8], out[16] = {0};
	snprintf(init, sizeof(init), "%06lX", (unsigned long)(c >> 8));

	SwkbdState kb;
	swkbdInit(&kb, SWKBD_TYPE_QWERTY, 2, 7);
	swkbdSetHintText(&kb, "Hex color, e.g. FF8800");
	swkbdSetInitialText(&kb, init);
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	if (swkbdInputText(&kb, out, sizeof(out)) != SWKBD_BUTTON_CONFIRM)
		return;

	char* p = out[0] == '#' ? out + 1 : out;
	size_t n = strlen(p);
	for (size_t i = 0; i < n; i++)
		if (!strchr("0123456789abcdefABCDEF", p[i]))
			return;
	unsigned long v;
	if (n == 6)
		v = strtoul(p, NULL, 16);
	else if (n == 3)
	{
		unsigned long s = strtoul(p, NULL, 16);
		unsigned long r = (s >> 8) & 0xF, g = (s >> 4) & 0xF, b = s & 0xF;
		v = (r * 17) << 16 | (g * 17) << 8 | (b * 17);
	}
	else
		return;
	set_rgba(((uint32_t)v << 8) | 0xFF);
}

int picker_list(uint32_t* out)
{
	memcpy(out, listColors, sizeof(uint32_t) * listCount);
	return listCount;
}

// Parse hex codes separated by anything that isn't a hex digit ("#", spaces, commas, newlines).
// Accepts 6- or 3-digit codes; returns false if any code is malformed.
static bool parse_list(const char* text)
{
	listCount = 0;
	const char* p = text;
	while (*p)
	{
		if (!isxdigit((unsigned char)*p)) { p++; continue; }
		const char* st = p;
		while (isxdigit((unsigned char)*p)) p++;
		int n = p - st;
		if ((n != 6 && n != 3) || listCount >= 32) return false;
		char tok[7] = {0};
		memcpy(tok, st, n);
		unsigned long v = strtoul(tok, NULL, 16);
		if (n == 3)
			v = ((v >> 8) & 0xF) * 0x110000 + ((v >> 4) & 0xF) * 0x1100 + (v & 0xF) * 0x11;
		listColors[listCount++] = ((uint32_t)v << 8) | 0xFF;
	}
	return listCount > 0;
}

static bool ask_list(void)
{
	static char init[32 * 7 + 1], out[400];
	init[0] = 0;
	for (int i = 0; i < 32; i++)
		snprintf(init + strlen(init), sizeof(init) - strlen(init), "%s%06lX", i ? " " : "", (unsigned long)(pal[i] >> 8));
	SwkbdState kb;
	swkbdInit(&kb, SWKBD_TYPE_QWERTY, 2, sizeof(out) - 1);
	swkbdSetFeatures(&kb, SWKBD_MULTILINE);
	swkbdSetHintText(&kb, "Hex codes for slots 1, 2, 3...");
	swkbdSetInitialText(&kb, init);
	memset(out, 0, sizeof(out));
	if (swkbdInputText(&kb, out, sizeof(out)) != SWKBD_BUTTON_CONFIRM)
		return false;
	if (parse_list(out))
		return true;
	listError = listCount >= 32 ? "More than 32 codes" : "Use 6-digit hex codes, e.g. FF8800";
	return false;
}

PickResult picker_update(u32 kDown, u32 kHeld, touchPosition t)
{
	if (kDown & KEY_A) return PICK_OK;
	if (kDown & KEY_B) return PICK_CANCEL;

	if (kDown & KEY_TOUCH)
	{
		drag = DRAG_NONE;
		if (in_rect(t, SQ_X - 6, SQ_Y - 6, SQ_W + 12, SQ_H + 8))
			drag = DRAG_SQUARE;
		else if (in_rect(t, SQ_X - 6, HUE_Y - 4, SQ_W + 12, HUE_H + 8))
			drag = DRAG_HUE;
		else if (in_rect(t, SQ_X, SWATCH_Y, SQ_W, SWATCH_H * 2))
		{
			int i = (int)((t.py - SWATCH_Y) / SWATCH_H) * 16 + (int)((t.px - SQ_X) / (SQ_W / 16.0f));
			if (i >= 0 && i < 32) set_rgba(pal[i]);
		}
		else if (in_rect(t, COL_X, PREV_Y, COL_W, 20))
			set_rgba(oldColor);  // tap the old color to go back to it
		else if (in_rect(t, COL_X, BTN_HEX_Y, COL_W, BTN_SMALL_H))
			ask_hex();
		else if (in_rect(t, COL_X, BTN_LIST_Y, COL_W, BTN_SMALL_H))
		{
			if (ask_list())
				return PICK_LIST;
		}
		else if (in_rect(t, COL_X, BTN_OK_Y, COL_W, BTN_H))
			return PICK_OK;
		else if (in_rect(t, COL_X, BTN_CANCEL_Y, COL_W, BTN_H))
			return PICK_CANCEL;
	}
	if (!(kHeld & KEY_TOUCH))
		drag = DRAG_NONE;

	if (drag == DRAG_SQUARE)
	{
		sat = clampf((t.px - SQ_X) / (float)SQ_W);
		val = 1.0f - clampf((t.py - SQ_Y) / (float)SQ_H);
	}
	else if (drag == DRAG_HUE)
		hue = clampf((t.px - SQ_X) / (float)SQ_W) * 359.9f;
	return PICK_OPEN;
}

static void button(int y, int h, const char* label, u32 bg)
{
	C2D_DrawRectSolid(COL_X, y, 0.5f, COL_W, h, bg);
	float w = gfx_text_width(0.5f, label);
	gfx_text(COL_X + (COL_W - w) / 2, y + (h - 14) / 2, 0.5f, C2D_Color32(255, 255, 255, 255), "%s", label);
}

static void ring(float x, float y, u32 fill)
{
	C2D_DrawCircleSolid(x, y, 0.5f, 7, C2D_Color32(0, 0, 0, 255));
	C2D_DrawCircleSolid(x, y, 0.5f, 6, C2D_Color32(255, 255, 255, 255));
	C2D_DrawCircleSolid(x, y, 0.5f, 4, fill);
}

void picker_draw(void)
{
	u32 white = C2D_Color32(255, 255, 255, 255), black = C2D_Color32(0, 0, 0, 255);
	u32 frame = C2D_Color32(90, 94, 104, 255);
	u32 c = current();

	gfx_text(8, 4, 0.5f, white, "Color %d", slot + 1);

	// saturation/brightness square: white -> hue across, then fade to black downward
	u32 pure = rgba_to_c2d(hsv_to_rgba(hue, 1, 1));
	C2D_DrawRectangle(SQ_X, SQ_Y, 0.5f, SQ_W, SQ_H, white, pure, white, pure);
	C2D_DrawRectangle(SQ_X, SQ_Y, 0.5f, SQ_W, SQ_H, C2D_Color32(0, 0, 0, 0), C2D_Color32(0, 0, 0, 0), black, black);
	gfx_rect_outline(SQ_X - 1, SQ_Y - 1, SQ_W + 2, SQ_H + 2, 1, frame);
	ring(SQ_X + sat * SQ_W, SQ_Y + (1 - val) * SQ_H, rgba_to_c2d(c));

	// hue bar
	for (int i = 0; i < 6; i++)
	{
		u32 a = rgba_to_c2d(hsv_to_rgba(i * 60.0f, 1, 1)), b = rgba_to_c2d(hsv_to_rgba((i + 1) * 60.0f, 1, 1));
		C2D_DrawRectangle(SQ_X + i * SQ_W / 6.0f, HUE_Y, 0.5f, SQ_W / 6.0f + 0.5f, HUE_H, a, b, a, b);
	}
	gfx_rect_outline(SQ_X - 1, HUE_Y - 1, SQ_W + 2, HUE_H + 2, 1, frame);
	float hx = SQ_X + hue / 360.0f * SQ_W;
	C2D_DrawRectSolid(hx - 3, HUE_Y - 3, 0.5f, 6, HUE_H + 6, black);
	C2D_DrawRectSolid(hx - 2, HUE_Y - 2, 0.5f, 4, HUE_H + 4, white);
	C2D_DrawRectSolid(hx - 1, HUE_Y, 0.5f, 2, HUE_H, pure);

	// project palette for quick picks
	for (int i = 0; i < 32; i++)
	{
		float x = SQ_X + (i % 16) * (SQ_W / 16.0f), y = SWATCH_Y + (i / 16) * SWATCH_H;
		C2D_DrawRectSolid(x + 0.5f, y + 0.5f, 0.5f, SQ_W / 16.0f - 1, SWATCH_H - 1, rgba_to_c2d(pal[i]));
	}

	// old / new
	gfx_draw_checker(COL_X, PREV_Y, COL_W, 40, 4);
	C2D_DrawRectSolid(COL_X, PREV_Y, 0.5f, COL_W, 20, rgba_to_c2d(oldColor));
	C2D_DrawRectSolid(COL_X, PREV_Y + 20, 0.5f, COL_W, 20, rgba_to_c2d(c));
	gfx_rect_outline(COL_X, PREV_Y, COL_W, 40, 1, white);
	gfx_text(COL_X + 2, PREV_Y + 3, 0.38f, (oldColor >> 8) > 0x808080 ? black : white, "old");
	gfx_text(COL_X + 2, PREV_Y + 23, 0.38f, (c >> 8) > 0x808080 ? black : white, "new");
	gfx_text(COL_X, 66, 0.6f, white, "#%06lX", (unsigned long)(c >> 8));
	gfx_text(COL_X, 84, 0.42f, C2D_Color32(170, 175, 190, 255), "R%lu G%lu B%lu",
	         (unsigned long)(c >> 24), (unsigned long)((c >> 16) & 255), (unsigned long)((c >> 8) & 255));

	button(BTN_HEX_Y, BTN_SMALL_H, "Hex...", C2D_Color32(70, 74, 90, 255));
	button(BTN_LIST_Y, BTN_SMALL_H, "Palette text...", C2D_Color32(70, 74, 90, 255));
	button(BTN_OK_Y, BTN_H, "OK (A)", C2D_Color32(40, 120, 70, 255));
	button(BTN_CANCEL_Y, BTN_H, "Cancel (B)", C2D_Color32(120, 50, 50, 255));

	if (listError)
		gfx_text(60, 4, 0.42f, C2D_Color32(255, 120, 100, 255), "%s", listError);
}
