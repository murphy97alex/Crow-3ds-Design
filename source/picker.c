// SPDX-License-Identifier: MIT
#include "picker.h"
#include "color.h"
#include "gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define WHEEL_X 8
#define WHEEL_Y 44
#define SLIDER_X 184
#define SLIDER_Y 44
#define SLIDER_W 24
#define SLIDER_H 160
#define COL_X 220
#define COL_W 92
#define BTN_H 24
#define BTN_HEX_Y 108
#define BTN_LIST_Y 136
#define BTN_OK_Y 172
#define BTN_CANCEL_Y 200

enum { DRAG_NONE, DRAG_WHEEL, DRAG_SLIDER };

static int slot;
static uint32_t oldColor;
static float hue, sat, val;
static float wheelVal = -1; // brightness the wheel texture was built with
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
	rgba_to_hsv(((uint32_t)v << 8) | 0xFF, &hue, &sat, &val);
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
		if (in_rect(t, WHEEL_X, WHEEL_Y, WHEEL_D, WHEEL_D))
			drag = DRAG_WHEEL;
		else if (in_rect(t, SLIDER_X - 6, SLIDER_Y - 6, SLIDER_W + 12, SLIDER_H + 12))
			drag = DRAG_SLIDER;
		else if (in_rect(t, COL_X, BTN_HEX_Y, COL_W, BTN_H))
			ask_hex();
		else if (in_rect(t, COL_X, BTN_LIST_Y, COL_W, BTN_H))
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

	if (drag == DRAG_WHEEL)
	{
		float r = WHEEL_D / 2.0f;
		float dx = t.px - (WHEEL_X + r), dy = t.py - (WHEEL_Y + r);
		float dist = sqrtf(dx * dx + dy * dy);
		hue = atan2f(-dy, dx) * (180.0f / (float)M_PI);
		if (hue < 0) hue += 360.0f;
		sat = dist >= r ? 1.0f : dist / r;
	}
	else if (drag == DRAG_SLIDER)
	{
		float v = 1.0f - (t.py - SLIDER_Y) / (float)SLIDER_H;
		val = v < 0 ? 0 : v > 1 ? 1 : v;
	}
	return PICK_OPEN;
}

static void button(int y, const char* label, u32 bg)
{
	C2D_DrawRectSolid(COL_X, y, 0.5f, COL_W, BTN_H, bg);
	float w = gfx_text_width(0.55f, label);
	gfx_text(COL_X + (COL_W - w) / 2, y + 4, 0.55f, C2D_Color32(255, 255, 255, 255), "%s", label);
}

void picker_draw(void)
{
	if (wheelVal != val)
	{
		gfx_wheel_build(val);
		wheelVal = val;
	}
	u32 white = C2D_Color32(255, 255, 255, 255), black = C2D_Color32(0, 0, 0, 255);
	u32 c = current();

	gfx_text(8, 8, 0.6f, white, "Color %d", slot + 1);
	gfx_text(8, 24, 0.45f, C2D_Color32(170, 175, 190, 255), "Wheel: hue/saturation   Bar: brightness");

	gfx_draw_wheel(WHEEL_X, WHEEL_Y);
	float r = WHEEL_D / 2.0f, a = hue * (float)M_PI / 180.0f;
	float mx = WHEEL_X + r + cosf(a) * sat * r, my = WHEEL_Y + r - sinf(a) * sat * r;
	C2D_DrawCircleSolid(mx, my, 0.5f, 6, black);
	C2D_DrawCircleSolid(mx, my, 0.5f, 4, white);
	C2D_DrawCircleSolid(mx, my, 0.5f, 3, rgba_to_c2d(c));

	u32 top = rgba_to_c2d(hsv_to_rgba(hue, sat, 1.0f));
	C2D_DrawRectangle(SLIDER_X, SLIDER_Y, 0.5f, SLIDER_W, SLIDER_H, top, top, black, black);
	gfx_rect_outline(SLIDER_X - 1, SLIDER_Y - 1, SLIDER_W + 2, SLIDER_H + 2, 1, C2D_Color32(90, 94, 104, 255));
	float sy = SLIDER_Y + (1.0f - val) * SLIDER_H;
	C2D_DrawRectSolid(SLIDER_X - 4, sy - 2, 0.5f, SLIDER_W + 8, 4, white);
	C2D_DrawRectSolid(SLIDER_X - 3, sy - 1, 0.5f, SLIDER_W + 6, 2, black);

	gfx_draw_checker(COL_X, WHEEL_Y, COL_W, 40, 4);
	C2D_DrawRectSolid(COL_X, WHEEL_Y, 0.5f, COL_W, 20, rgba_to_c2d(oldColor));
	C2D_DrawRectSolid(COL_X, WHEEL_Y + 20, 0.5f, COL_W, 20, rgba_to_c2d(c));
	gfx_rect_outline(COL_X, WHEEL_Y, COL_W, 40, 1, white);
	gfx_text(COL_X, WHEEL_Y + 46, 0.6f, white, "#%06lX", (unsigned long)(c >> 8));
	if (listError)
		gfx_text(8, 222, 0.45f, C2D_Color32(255, 120, 100, 255), "Palette text: %s", listError);

	button(BTN_HEX_Y, "Hex...", C2D_Color32(70, 74, 90, 255));
	button(BTN_LIST_Y, "Palette text...", C2D_Color32(70, 74, 90, 255));
	button(BTN_OK_Y, "OK (A)", C2D_Color32(40, 120, 70, 255));
	button(BTN_CANCEL_Y, "Cancel (B)", C2D_Color32(120, 50, 50, 255));
}
