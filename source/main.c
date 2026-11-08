// 3DS Pixel UI Designer
// SPDX-License-Identifier: MIT

#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "color.h"
#include "doc.h"
#include "gfx.h"
#include "picker.h"
#include "pxd.h"
#include "tools.h"

#define APP_NAME "Pixel UI Designer"
#define SAVE_ROOT "sdmc:/3ds/pixel-ui-designer"
#define SAVE_DIR SAVE_ROOT "/projects"

// Bottom screen layout
#define BOT_W 320
#define BOT_H 240
#define SW_COLS 16
#define SW_W 20
#define SW_H 11
#define STRIP_H (SW_H * 2)  // palette strip at the top of the bottom screen
#define VIEW_Y STRIP_H
#define VIEW_H (BOT_H - VIEW_Y)

#define COL(r, g, b) C2D_Color32(r, g, b, 255)
#define C_BG_TOP   COL(0x20, 0x22, 0x28)
#define C_BG_BOT   COL(0x30, 0x32, 0x3A)
#define C_PANEL    COL(0x2C, 0x2F, 0x38)
#define C_TEXT     COL(0xF0, 0xF0, 0xF0)
#define C_DIM      COL(0xA0, 0xA4, 0xB0)
#define C_ACCENT   COL(0xFF, 0xAA, 0x28)
#define C_SEL      COL(0x44, 0x4A, 0x5C)

// ---- tools -----------------------------------------------------------------

typedef enum { TOOL_PENCIL, TOOL_ERASER, TOOL_FILL, TOOL_LINE, TOOL_RECT, TOOL_ELLIPSE, TOOL_EYEDROPPER, TOOL_COUNT } Tool;
static const char* TOOL_NAMES[TOOL_COUNT] = { "Pencil", "Eraser", "Fill", "Line", "Rectangle", "Ellipse", "Eyedropper" };

static bool is_shape(Tool t) { return t == TOOL_LINE || t == TOOL_RECT || t == TOOL_ELLIPSE; }

// per-tool options
static Brush brushes[TOOL_COUNT] = {
	[TOOL_PENCIL] = { 1, BRUSH_SQUARE, false }, [TOOL_ERASER] = { 3, BRUSH_SQUARE, false },
	[TOOL_LINE] = { 1, BRUSH_SQUARE, false },   [TOOL_RECT] = { 1, BRUSH_SQUARE, false },
	[TOOL_ELLIPSE] = { 1, BRUSH_SQUARE, false },
};
static bool shapeFilled[TOOL_COUNT];
static bool fillReplace;
static Mirror mirrorMode = MIRROR_OFF;

static const char* SHAPE_NAMES[BRUSH_SHAPE_COUNT] = { "Square", "Round", "Dither" };
static const char* MIRROR_NAMES[MIRROR_COUNT] = { "Off", "Left-right", "Top-bottom", "Both" };

// options panel rows
typedef enum { OPT_SIZE, OPT_SHAPE, OPT_PIXPERF, OPT_STYLE, OPT_FILLMODE, OPT_MIRROR } OptKind;
static int optSel;

// ---- app state ---------------------------------------------------------------

typedef enum { MODE_DRAW, MODE_MENU, MODE_NEW, MODE_OPEN, MODE_CONFIRM, MODE_PICKER, MODE_OPTIONS } Mode;
typedef enum { ACT_NONE, ACT_NEW, ACT_OPEN, ACT_EXIT, ACT_OVERWRITE } Action;

static Doc doc;
static Mode mode = MODE_DRAW;
static Tool tool = TOOL_PENCIL, lastTool = TOOL_ERASER;
static int color = 0;          // current palette slot
static int zoomIdx;
static float viewX, viewY;     // canvas coords at the view's top-left corner
static bool gridOn = true;
static bool previewMode = false;
static bool running = true;

static const int ZOOMS[] = { 1, 2, 3, 4, 6, 8, 12, 16, 24 };
#define ZOOM_COUNT (int)(sizeof(ZOOMS) / sizeof(ZOOMS[0]))

// touch / stroke
enum { TOUCH_NONE, TOUCH_CANVAS, TOUCH_STRIP };
static int touchTarget = TOUCH_NONE;
static int startCX, startCY;   // where the current stroke/shape began
static bool strokeActive;

// menus
static int menuSel, newSel, openSel, openCount;
static char openFiles[128][DOC_NAME_MAX + 8];
static Action confirmAction;
static char confirmMsg[96];
static int pendingNewW, pendingNewH;
static char pendingName[DOC_NAME_MAX];

static char status[96];
static int statusTimer;

static void set_status(const char* msg)
{
	snprintf(status, sizeof(status), "%s", msg);
	statusTimer = 180;
}

// ---- view --------------------------------------------------------------------

static int zoom(void) { return ZOOMS[zoomIdx]; }

static void view_clamp(void)
{
	float z = zoom();
	float cx = viewX + BOT_W / 2.0f / z, cy = viewY + VIEW_H / 2.0f / z;
	if (cx < 0) viewX -= cx;
	if (cy < 0) viewY -= cy;
	if (cx > doc.w) viewX -= cx - doc.w;
	if (cy > doc.h) viewY -= cy - doc.h;
}

static void view_fit(void)
{
	zoomIdx = 0;
	for (int i = ZOOM_COUNT - 1; i >= 0; i--)
		if (doc.w * ZOOMS[i] <= BOT_W && doc.h * ZOOMS[i] <= VIEW_H)
		{
			zoomIdx = i;
			break;
		}
	viewX = (doc.w - BOT_W / (float)zoom()) / 2.0f;
	viewY = (doc.h - VIEW_H / (float)zoom()) / 2.0f;
}

static void view_zoom(int dir)
{
	int ni = zoomIdx + dir;
	if (ni < 0 || ni >= ZOOM_COUNT)
		return;
	float cx = viewX + BOT_W / 2.0f / zoom(), cy = viewY + VIEW_H / 2.0f / zoom();
	zoomIdx = ni;
	viewX = cx - BOT_W / 2.0f / zoom();
	viewY = cy - VIEW_H / 2.0f / zoom();
	view_clamp();
}

// Screen position of the canvas' top-left corner, rounded so pixels stay crisp.
static float canvas_sx(void) { return floorf(-viewX * zoom() + 0.5f); }
static float canvas_sy(void) { return floorf(VIEW_Y - viewY * zoom() + 0.5f); }

static void screen_to_canvas(int sx, int sy, int* cx, int* cy)
{
	*cx = (int)floorf((sx - canvas_sx()) / zoom());
	*cy = (int)floorf((sy - canvas_sy()) / zoom());
}

// ---- editing -------------------------------------------------------------------

static void set_tool(Tool t)
{
	if (t == tool)
		return;
	lastTool = tool;
	tool = t;
}

static void eyedrop(int cx, int cy)
{
	if (cx < 0 || cy < 0 || cx >= doc.w || cy >= doc.h)
		return;
	uint8_t v = doc_get(&doc, cx, cy);
	if (v != PIX_TRANSPARENT)
		color = v - 1;
}

static void after_doc_change(void)
{
	gfx_canvas_rebuild(&doc);
}

// ---- tool options ----------------------------------------------------------------

static int opt_rows(Tool t, OptKind* rows)
{
	int n = 0;
	switch (t)
	{
	case TOOL_PENCIL:
		rows[n++] = OPT_SIZE; rows[n++] = OPT_SHAPE; rows[n++] = OPT_PIXPERF; break;
	case TOOL_ERASER:
		rows[n++] = OPT_SIZE; rows[n++] = OPT_SHAPE; break;
	case TOOL_FILL:
		rows[n++] = OPT_FILLMODE; break;
	case TOOL_LINE:
		rows[n++] = OPT_SIZE; rows[n++] = OPT_SHAPE; break;
	case TOOL_RECT:
	case TOOL_ELLIPSE:
		rows[n++] = OPT_STYLE; rows[n++] = OPT_SIZE; break;
	default:
		break;
	}
	if (t != TOOL_EYEDROPPER)
		rows[n++] = OPT_MIRROR;
	return n;
}

static const char* opt_label(OptKind k)
{
	switch (k)
	{
	case OPT_SIZE: return "Size";
	case OPT_SHAPE: return "Brush";
	case OPT_PIXPERF: return "Pixel-perfect";
	case OPT_STYLE: return "Style";
	case OPT_FILLMODE: return "Mode";
	case OPT_MIRROR: return "Mirror";
	}
	return "";
}

static void opt_value(OptKind k, char* out, size_t n)
{
	Brush* b = &brushes[tool];
	switch (k)
	{
	case OPT_SIZE: snprintf(out, n, "%d px", b->size); break;
	case OPT_SHAPE: snprintf(out, n, "%s", SHAPE_NAMES[b->shape]); break;
	case OPT_PIXPERF: snprintf(out, n, "%s%s", b->pixelPerfect ? "On" : "Off", b->pixelPerfect && b->size > 1 ? " (size 1 only)" : ""); break;
	case OPT_STYLE: snprintf(out, n, "%s", shapeFilled[tool] ? "Filled" : "Outline"); break;
	case OPT_FILLMODE: snprintf(out, n, "%s", fillReplace ? "Replace color" : "Connected area"); break;
	case OPT_MIRROR: snprintf(out, n, "%s", MIRROR_NAMES[mirrorMode]); break;
	}
}

static void opt_change(OptKind k, int dir)
{
	Brush* b = &brushes[tool];
	switch (k)
	{
	case OPT_SIZE:
		b->size += dir;
		if (b->size < 1) b->size = 1;
		if (b->size > BRUSH_MAX) b->size = BRUSH_MAX;
		break;
	case OPT_SHAPE:
	{
		int count = tool == TOOL_LINE ? 2 : BRUSH_SHAPE_COUNT; // no dither lines
		b->shape = (b->shape + count + dir) % count;
		break;
	}
	case OPT_PIXPERF: b->pixelPerfect = !b->pixelPerfect; break;
	case OPT_STYLE: shapeFilled[tool] = !shapeFilled[tool]; break;
	case OPT_FILLMODE: fillReplace = !fillReplace; break;
	case OPT_MIRROR: mirrorMode = (mirrorMode + MIRROR_COUNT + dir) % MIRROR_COUNT; break;
	}
}

static void update_options(u32 kDown)
{
	OptKind rows[8];
	int n = opt_rows(tool, rows);
	if (kDown & (KEY_A | KEY_B)) { mode = MODE_DRAW; return; }
	if (n == 0) return;
	if (optSel >= n) optSel = n - 1;
	if (kDown & KEY_DUP) optSel = (optSel + n - 1) % n;
	if (kDown & KEY_DDOWN) optSel = (optSel + 1) % n;
	if (kDown & KEY_DLEFT) opt_change(rows[optSel], -1);
	if (kDown & KEY_DRIGHT) opt_change(rows[optSel], +1);
}

// Short summary of the tool's settings for the tool list, e.g. "3 Round".
static void tool_summary(Tool t, char* out, size_t n)
{
	const Brush* b = &brushes[t];
	switch (t)
	{
	case TOOL_PENCIL:
		snprintf(out, n, "%d %s%s", b->size, SHAPE_NAMES[b->shape], b->pixelPerfect && b->size == 1 ? " PP" : ""); break;
	case TOOL_ERASER:
	case TOOL_LINE:
		snprintf(out, n, "%d %s", b->size, SHAPE_NAMES[b->shape]); break;
	case TOOL_RECT:
	case TOOL_ELLIPSE:
		if (shapeFilled[t]) snprintf(out, n, "Filled");
		else snprintf(out, n, "Outline %d", b->size);
		break;
	case TOOL_FILL: snprintf(out, n, "%s", fillReplace ? "Replace" : "Area"); break;
	default: out[0] = 0; break;
	}
}

// ---- files ---------------------------------------------------------------------

static void ensure_dirs(void)
{
	mkdir("sdmc:/3ds", 0777);
	mkdir(SAVE_ROOT, 0777);
	mkdir(SAVE_DIR, 0777);
}

static void project_path(char* out, size_t n, const char* name)
{
	snprintf(out, n, SAVE_DIR "/%s.pxd", name);
}

static bool file_exists(const char* path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

static void save_as_name(const char* name)
{
	ensure_dirs();
	char path[160];
	project_path(path, sizeof(path), name);
	const char* err = pxd_save(&doc, path);
	if (err)
	{
		char msg[96];
		snprintf(msg, sizeof(msg), "Save failed: %s", err);
		set_status(msg);
		return;
	}
	snprintf(doc.name, sizeof(doc.name), "%s", name);
	doc.dirty = false;
	char msg[96];
	snprintf(msg, sizeof(msg), "Saved %s.pxd", name);
	set_status(msg);
}

static void confirm(Action a, const char* msg)
{
	confirmAction = a;
	snprintf(confirmMsg, sizeof(confirmMsg), "%s", msg);
	mode = MODE_CONFIRM;
}

// Returns false if cancelled.
static bool ask_name(char* out, size_t n)
{
	SwkbdState kb;
	char buf[DOC_NAME_MAX] = {0};
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, DOC_NAME_MAX - 1);
	swkbdSetHintText(&kb, "Project name");
	swkbdSetInitialText(&kb, doc.name[0] ? doc.name : "untitled");
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	if (swkbdInputText(&kb, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM)
		return false;
	// keep file names simple and portable
	size_t j = 0;
	for (size_t i = 0; buf[i] && j < n - 1; i++)
	{
		char c = buf[i];
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
		out[j++] = ok ? c : (c == ' ' ? '_' : 0);
		if (!out[j - 1]) j--;
	}
	out[j] = '\0';
	return j > 0;
}

static void save_as(void)
{
	if (!ask_name(pendingName, sizeof(pendingName)))
		return;
	char path[160];
	project_path(path, sizeof(path), pendingName);
	if (file_exists(path) && strcmp(pendingName, doc.name) != 0)
	{
		char msg[96];
		snprintf(msg, sizeof(msg), "Overwrite %s.pxd?", pendingName);
		confirm(ACT_OVERWRITE, msg);
		return;
	}
	save_as_name(pendingName);
	mode = MODE_DRAW;
}

static void scan_projects(void)
{
	openCount = 0;
	DIR* dir = opendir(SAVE_DIR);
	if (!dir)
		return;
	struct dirent* e;
	while ((e = readdir(dir)) && openCount < 128)
	{
		size_t n = strlen(e->d_name);
		if (n > 4 && n - 4 < DOC_NAME_MAX && !strcasecmp(e->d_name + n - 4, ".pxd"))
		{
			memcpy(openFiles[openCount], e->d_name, n - 4);
			openFiles[openCount][n - 4] = '\0';
			openCount++;
		}
	}
	closedir(dir);
	qsort(openFiles, openCount, sizeof(openFiles[0]), (int (*)(const void*, const void*))strcasecmp);
}

static void open_project(const char* name)
{
	char path[160];
	project_path(path, sizeof(path), name);
	const char* err = pxd_load(&doc, path);
	if (err)
	{
		char msg[96];
		snprintf(msg, sizeof(msg), "Open failed: %s", err);
		set_status(msg);
		return;
	}
	snprintf(doc.name, sizeof(doc.name), "%s", name);
	view_fit();
	after_doc_change();
	set_status("Opened");
}

static void new_canvas(int w, int h)
{
	if (!doc_new(&doc, w, h))
	{
		set_status("Couldn't create canvas");
		return;
	}
	color = 0;
	view_fit();
	after_doc_change();
}

static void run_action(Action a)
{
	mode = MODE_DRAW;
	switch (a)
	{
	case ACT_NEW: new_canvas(pendingNewW, pendingNewH); break;
	case ACT_OPEN: open_project(openFiles[openSel]); break;
	case ACT_EXIT: running = false; break;
	case ACT_OVERWRITE: save_as_name(pendingName); break;
	default: break;
	}
}

// ---- menus ---------------------------------------------------------------------

static const char* MENU_ITEMS[] = { "Back to canvas", "New canvas", "Save", "Save as...", "Open...", "Exit" };
#define MENU_COUNT 6

typedef struct { const char* label; int w, h; } Preset;
static const Preset PRESETS[] = {
	{ "16 x 16", 16, 16 }, { "32 x 32", 32, 32 }, { "64 x 64", 64, 64 }, { "128 x 64", 128, 64 },
	{ "128 x 128", 128, 128 }, { "240 x 240", 240, 240 }, { "320 x 240", 320, 240 }, { "Custom...", 0, 0 },
};
#define PRESET_COUNT 8

static int ask_number(const char* hint, int def, int max)
{
	SwkbdState kb;
	char buf[8] = {0}, init[8];
	snprintf(init, sizeof(init), "%d", def);
	swkbdInit(&kb, SWKBD_TYPE_NUMPAD, 2, 3);
	swkbdSetHintText(&kb, hint);
	swkbdSetInitialText(&kb, init);
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	if (swkbdInputText(&kb, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM)
		return 0;
	int v = atoi(buf);
	return v < 1 ? 0 : v > max ? max : v;
}

static void request_new(int w, int h)
{
	pendingNewW = w;
	pendingNewH = h;
	if (doc.dirty)
		confirm(ACT_NEW, "Discard unsaved changes?");
	else
		run_action(ACT_NEW);
}

static void list_nav(u32 kDown, int* sel, int count)
{
	if (count <= 0)
		return;
	if (kDown & KEY_DUP) *sel = (*sel + count - 1) % count;
	if (kDown & KEY_DDOWN) *sel = (*sel + 1) % count;
}

static void update_menus(u32 kDown)
{
	switch (mode)
	{
	case MODE_MENU:
		list_nav(kDown, &menuSel, MENU_COUNT);
		if (kDown & (KEY_B | KEY_START)) mode = MODE_DRAW;
		if (kDown & KEY_A)
		{
			switch (menuSel)
			{
			case 0: mode = MODE_DRAW; break;
			case 1: mode = MODE_NEW; break;
			case 2:
				if (doc.name[0]) { save_as_name(doc.name); mode = MODE_DRAW; }
				else save_as();
				break;
			case 3: save_as(); break;
			case 4: scan_projects(); openSel = 0; mode = MODE_OPEN; break;
			case 5:
				if (doc.dirty) confirm(ACT_EXIT, "Exit without saving?");
				else running = false;
				break;
			}
		}
		break;
	case MODE_NEW:
		list_nav(kDown, &newSel, PRESET_COUNT);
		if (kDown & KEY_B) mode = MODE_MENU;
		if (kDown & KEY_A)
		{
			const Preset* p = &PRESETS[newSel];
			if (p->w)
				request_new(p->w, p->h);
			else
			{
				int w = ask_number("Width (1-320)", doc.w, DOC_MAX_W);
				int h = w ? ask_number("Height (1-240)", doc.h, DOC_MAX_H) : 0;
				if (w && h)
					request_new(w, h);
			}
		}
		break;
	case MODE_OPEN:
		list_nav(kDown, &openSel, openCount);
		if (kDown & KEY_B) mode = MODE_MENU;
		if ((kDown & KEY_A) && openCount > 0)
		{
			if (doc.dirty) confirm(ACT_OPEN, "Discard unsaved changes?");
			else run_action(ACT_OPEN);
		}
		break;
	case MODE_CONFIRM:
		if (kDown & KEY_A) run_action(confirmAction);
		else if (kDown & KEY_B) mode = MODE_MENU;
		break;
	default:
		break;
	}
}

// ---- draw mode input -------------------------------------------------------------

static void update_draw(u32 kDown, u32 kHeld, u32 kUp, touchPosition touch)
{
	if (kDown & KEY_START) { mode = MODE_MENU; menuSel = 0; return; }
	if ((kDown & KEY_A) && touchTarget == TOUCH_NONE) { mode = MODE_OPTIONS; optSel = 0; return; }
	if (kDown & KEY_SELECT) previewMode = !previewMode;
	if (kDown & KEY_X) gridOn = !gridOn;
	if (touchTarget == TOUCH_NONE) // no tool changes mid-stroke
	{
		if (kDown & KEY_Y) set_tool(lastTool);
		if (kDown & KEY_DLEFT) set_tool((tool + TOOL_COUNT - 1) % TOOL_COUNT);
		if (kDown & KEY_DRIGHT) set_tool((tool + 1) % TOOL_COUNT);
	}
	if (kDown & KEY_DUP) view_zoom(+1);
	if (kDown & KEY_DDOWN) view_zoom(-1);

	if (touchTarget == TOUCH_NONE)
	{
		if ((kDown & KEY_L) && hist_undo(&doc)) after_doc_change();
		if ((kDown & KEY_ZL) && hist_redo(&doc)) after_doc_change();
	}

	circlePosition cp;
	hidCircleRead(&cp);
	if (abs(cp.dx) > 16 || abs(cp.dy) > 16)
	{
		const float speed = 5.0f / 156.0f;
		viewX += cp.dx * speed / zoom();
		viewY -= cp.dy * speed / zoom();
		view_clamp();
	}

	bool dropper = (kHeld & KEY_R) || tool == TOOL_EYEDROPPER;
	uint8_t value = tool == TOOL_ERASER ? PIX_TRANSPARENT : color + 1;

	if (kDown & KEY_TOUCH)
	{
		if (touch.py < STRIP_H)
		{
			touchTarget = TOUCH_STRIP;
			int slot = (touch.py / SW_H) * SW_COLS + touch.px / SW_W;
			if (slot >= 0 && slot < DOC_PALETTE_SIZE)
			{
				if (slot == color)
				{
					picker_open(slot, doc.palette[slot]);
					mode = MODE_PICKER;
					touchTarget = TOUCH_NONE;
					return;
				}
				color = slot;
				if (tool == TOOL_ERASER || tool == TOOL_EYEDROPPER)
					set_tool(TOOL_PENCIL);
			}
		}
		else
		{
			touchTarget = TOUCH_CANVAS;
			int cx, cy;
			screen_to_canvas(touch.px, touch.py, &cx, &cy);
			startCX = cx;
			startCY = cy;
			if (dropper)
				eyedrop(cx, cy);
			else
			{
				hist_begin(&doc);
				tools_begin(&doc, mirrorMode);
				strokeActive = true;
				if (tool == TOOL_FILL)
					tools_fill(cx, cy, color + 1, fillReplace);
				else if (tool == TOOL_PENCIL || tool == TOOL_ERASER)
					tools_stroke_start(cx, cy, value, &brushes[tool]);
				// shapes are drawn below while dragging
			}
		}
	}

	if ((kHeld & KEY_TOUCH) && touchTarget == TOUCH_CANVAS)
	{
		int cx, cy;
		screen_to_canvas(touch.px, touch.py, &cx, &cy);
		if (dropper && !strokeActive)
			eyedrop(cx, cy);
		else if (strokeActive && (tool == TOOL_PENCIL || tool == TOOL_ERASER) && !(kDown & KEY_TOUCH))
			tools_stroke_to(cx, cy, value, &brushes[tool]);
		else if (strokeActive && is_shape(tool))
		{
			if (kDown & KEY_B)
			{
				// cancel the shape
				tools_restore();
				hist_abort();
				strokeActive = false;
				touchTarget = TOUCH_STRIP; // ignore the rest of this touch
			}
			else
			{
				tools_restore();
				bool snap = kHeld & KEY_ZR;
				if (tool == TOOL_LINE)
				{
					if (snap) tools_snap_line(startCX, startCY, &cx, &cy);
					tools_line(startCX, startCY, cx, cy, value, &brushes[tool]);
				}
				else
				{
					if (snap) tools_snap_square(startCX, startCY, &cx, &cy);
					int sz = brushes[tool].size;
					if (tool == TOOL_RECT)
						tools_rect(startCX, startCY, cx, cy, value, sz, shapeFilled[tool]);
					else
						tools_ellipse(startCX, startCY, cx, cy, value, sz, shapeFilled[tool]);
				}
			}
		}
		if (strokeActive || (kDown & KEY_B))
			after_doc_change();
	}
	else if ((kDown & KEY_TOUCH) && strokeActive)
		after_doc_change();

	if ((kUp & KEY_TOUCH) || !(kHeld & KEY_TOUCH))
	{
		if (touchTarget == TOUCH_CANVAS && strokeActive)
		{
			if (tools_changed())
			{
				hist_commit();
				doc.dirty = true;
			}
			else
				hist_abort();
			after_doc_change();
		}
		if (touchTarget == TOUCH_CANVAS && tool == TOOL_EYEDROPPER && !(kHeld & KEY_R))
			set_tool(lastTool); // one-shot eyedropper tool returns to the previous tool
		strokeActive = false;
		touchTarget = TOUCH_NONE;
	}
}

// ---- rendering --------------------------------------------------------------------

static void draw_bottom(void)
{
	if (mode == MODE_PICKER)
	{
		picker_draw();
		return;
	}

	float z = zoom(), sx = canvas_sx(), sy = canvas_sy();
	gfx_draw_canvas(&doc, sx, sy, z, VIEW_Y, BOT_H);

	if (gridOn && z >= 4)
	{
		u32 gc = C2D_Color32(0, 0, 0, 0x50);
		int x0 = viewX < 0 ? 0 : (int)viewX, x1 = (int)(viewX + BOT_W / z) + 1;
		int y0 = viewY < 0 ? 0 : (int)viewY, y1 = (int)(viewY + VIEW_H / z) + 1;
		if (x1 > doc.w) x1 = doc.w;
		if (y1 > doc.h) y1 = doc.h;
		float top = sy < VIEW_Y ? VIEW_Y : sy, bot = sy + doc.h * z > BOT_H ? BOT_H : sy + doc.h * z;
		float left = sx < 0 ? 0 : sx, right = sx + doc.w * z > BOT_W ? BOT_W : sx + doc.w * z;
		for (int x = x0 + 1; x < x1; x++)
			C2D_DrawRectSolid(sx + x * z, top, 0.5f, 1, bot - top, gc);
		for (int y = y0 + 1; y < y1; y++)
		{
			float ly = sy + y * z;
			if (ly >= VIEW_Y)
				C2D_DrawRectSolid(left, ly, 0.5f, right - left, 1, gc);
		}
	}
	// canvas border
	u32 bc = C2D_Color32(0x80, 0x84, 0x90, 255);
	float bx = sx - 1, by = sy - 1, bw = doc.w * z + 2, bh = doc.h * z + 2;
	if (by >= VIEW_Y) C2D_DrawRectSolid(bx, by, 0.5f, bw, 1, bc);
	if (by + bh - 1 >= VIEW_Y) C2D_DrawRectSolid(bx, by + bh - 1, 0.5f, bw, 1, bc);
	float vy0 = by < VIEW_Y ? VIEW_Y : by, vy1 = by + bh;
	if (vy1 > vy0)
	{
		C2D_DrawRectSolid(bx, vy0, 0.5f, 1, vy1 - vy0, bc);
		C2D_DrawRectSolid(bx + bw - 1, vy0, 0.5f, 1, vy1 - vy0, bc);
	}

	// mirror guides
	if (mirrorMode != MIRROR_OFF)
	{
		u32 mc = C2D_Color32(0xFF, 0x50, 0xC8, 0xB0);
		if (mirrorMode == MIRROR_X || mirrorMode == MIRROR_XY)
			C2D_DrawRectSolid(sx + doc.w * z / 2 - (z >= 4 ? 1 : 0.5f), sy < VIEW_Y ? VIEW_Y : sy, 0.5f, z >= 4 ? 2 : 1, doc.h * z, mc);
		if (mirrorMode == MIRROR_Y || mirrorMode == MIRROR_XY)
		{
			float gy = sy + doc.h * z / 2;
			if (gy >= VIEW_Y)
				C2D_DrawRectSolid(sx, gy - (z >= 4 ? 1 : 0.5f), 0.5f, doc.w * z, z >= 4 ? 2 : 1, mc);
		}
	}

	// palette strip
	C2D_DrawRectSolid(0, 0, 0.5f, BOT_W, STRIP_H, C_PANEL);
	for (int i = 0; i < DOC_PALETTE_SIZE; i++)
	{
		float x = (i % SW_COLS) * SW_W, y = (i / SW_COLS) * SW_H;
		C2D_DrawRectSolid(x + 1, y + 1, 0.5f, SW_W - 2, SW_H - 2, rgba_to_c2d(doc.palette[i]));
	}
	float hx = (color % SW_COLS) * SW_W, hy = (color / SW_COLS) * SW_H;
	gfx_rect_outline(hx, hy, SW_W, SW_H, 2, C2D_Color32(0, 0, 0, 255));
	gfx_rect_outline(hx + 1, hy + 1, SW_W - 2, SW_H - 2, 1, C2D_Color32(255, 255, 255, 255));

	if (mode != MODE_DRAW)
		C2D_DrawRectSolid(0, 0, 0.5f, BOT_W, BOT_H, C2D_Color32(0, 0, 0, 0x90));
}

// Draws the canvas into a box at the largest scale <= 1:1 that fits, centered.
static void draw_preview_box(float x, float y, float w, float h)
{
	float s = 1.0f;
	while (doc.w * s > w || doc.h * s > h)
		s /= 2;
	float px = floorf(x + (w - doc.w * s) / 2), py = floorf(y + (h - doc.h * s) / 2);
	gfx_draw_canvas(&doc, px, py, s, 0, 240);
	if (s < 1.0f)
		gfx_text(x + 4, y + h - 14, 0.4f, C_DIM, "1:%d", (int)(1 / s));
}

static void draw_top_tools(void)
{
	// title bar
	C2D_DrawRectSolid(0, 0, 0.5f, 400, 22, C_PANEL);
	gfx_text(8, 3, 0.55f, C_TEXT, APP_NAME);
	char name[80];
	snprintf(name, sizeof(name), "%s%s", doc.name[0] ? doc.name : "untitled", doc.dirty ? " *" : "");
	gfx_text(392 - gfx_text_width(0.5f, name), 4, 0.5f, C_DIM, "%s", name);

	// tool list
	gfx_text(10, 26, 0.42f, C_DIM, "TOOLS  D-pad < >   A: options");
	for (int i = 0; i < TOOL_COUNT; i++)
	{
		float y = 40 + i * 15;
		if (i == tool)
			C2D_DrawRectSolid(8, y - 1, 0.5f, 180, 15, C_SEL);
		gfx_text(14, y, 0.48f, i == tool ? C_ACCENT : C_TEXT, "%s", TOOL_NAMES[i]);
		char sum[32];
		tool_summary(i, sum, sizeof(sum));
		if (sum[0])
			gfx_text(96, y + 1, 0.42f, C_DIM, "%s", sum);
	}

	// color
	float cy = 40 + TOOL_COUNT * 15 + 6;
	C2D_DrawRectSolid(10, cy, 0.5f, 22, 22, C_TEXT);
	C2D_DrawRectSolid(11, cy + 1, 0.5f, 20, 20, rgba_to_c2d(doc.palette[color]));
	gfx_text(40, cy + 4, 0.5f, C_TEXT, "Color %d  #%06lX", color + 1, (unsigned long)(doc.palette[color] >> 8));

	// info
	float iy = cy + 28;
	gfx_text(10, iy, 0.42f, C_DIM, "Canvas %dx%d  Zoom %dx  Grid %s", doc.w, doc.h, zoom(), gridOn ? "on" : "off");
	gfx_text(10, iy + 12, 0.42f, C_DIM, "Frame %d/%d  Mirror %s", doc.curFrame + 1, doc.frameCount, MIRROR_NAMES[mirrorMode]);

	// true-size preview
	C2D_DrawRectSolid(196, 28, 0.5f, 198, 180, C_BG_BOT);
	draw_preview_box(197, 29, 196, 178);

	// footer
	if (statusTimer > 0)
		gfx_text(10, 220, 0.5f, C_ACCENT, "%s", status);
	else if (mode == MODE_PICKER)
		gfx_text(10, 220, 0.45f, C_DIM, "Pick a color below.  A: OK   B: cancel");
	else if (is_shape(tool))
		gfx_text(10, 222, 0.42f, C_DIM, "Drag to draw. Hold ZR to snap, B cancels.");
	else
		gfx_text(10, 222, 0.42f, C_DIM, "L undo  ZL redo  R eyedrop  X grid  START menu");
}

static void draw_top_preview(void)
{
	// show the canvas at 1x, 2x, 3x... side by side, as many as fit
	float gap = 12, total = 0;
	int n = 0;
	for (int s = 1; s <= 4; s++)
	{
		if (doc.h * s > 210 || total + doc.w * s + (n ? gap : 0) > 384)
			break;
		total += doc.w * s + (n ? gap : 0);
		n++;
	}
	if (n == 0)
	{
		draw_preview_box(0, 0, 400, 240);
		return;
	}
	float x = floorf((400 - total) / 2);
	for (int s = 1; s <= n; s++)
	{
		float y = floorf((226 - doc.h * s) / 2);
		gfx_draw_canvas(&doc, x, y, s, 0, 240);
		gfx_text(x, y + doc.h * s + 2, 0.4f, C_DIM, "%dx", s);
		x += doc.w * s + gap;
	}
	gfx_text(8, 224, 0.42f, C_DIM, "Preview   SELECT: back to tools");
}

static void draw_list(const char* title, const char* const* items, int count, int sel, const char* empty)
{
	C2D_DrawRectSolid(60, 20, 0.5f, 280, 200, C_PANEL);
	gfx_rect_outline(60, 20, 280, 200, 1, C_DIM);
	gfx_text(72, 28, 0.6f, C_TEXT, "%s", title);
	if (count == 0)
		gfx_text(72, 60, 0.5f, C_DIM, "%s", empty);
	int first = sel - 6 < 0 ? 0 : sel - 6;
	for (int i = first; i < count && i < first + 8; i++)
	{
		float y = 52 + (i - first) * 19;
		if (i == sel)
			C2D_DrawRectSolid(66, y - 2, 0.5f, 268, 18, C_SEL);
		gfx_text(76, y, 0.52f, i == sel ? C_ACCENT : C_TEXT, "%s", items[i]);
	}
	gfx_text(72, 204, 0.42f, C_DIM, "Up/Down select   A: OK   B: back");
}

static void draw_top(void)
{
	if (previewMode && mode == MODE_DRAW)
		draw_top_preview();
	else
		draw_top_tools();

	if (mode == MODE_MENU)
		draw_list("Menu", MENU_ITEMS, MENU_COUNT, menuSel, "");
	else if (mode == MODE_NEW)
	{
		const char* labels[PRESET_COUNT];
		for (int i = 0; i < PRESET_COUNT; i++) labels[i] = PRESETS[i].label;
		draw_list("New canvas", labels, PRESET_COUNT, newSel, "");
	}
	else if (mode == MODE_OPEN)
	{
		static const char* labels[128];
		for (int i = 0; i < openCount; i++) labels[i] = openFiles[i];
		draw_list("Open project", labels, openCount, openSel, "No projects in /3ds/pixel-ui-designer/projects");
	}
	else if (mode == MODE_OPTIONS)
	{
		OptKind rows[8];
		int n = opt_rows(tool, rows);
		C2D_DrawRectSolid(40, 40, 0.5f, 320, 40 + n * 22 + 24, C_PANEL);
		gfx_rect_outline(40, 40, 320, 40 + n * 22 + 24, 1, C_ACCENT);
		gfx_text(52, 48, 0.6f, C_TEXT, "%s options", TOOL_NAMES[tool]);
		if (n == 0)
			gfx_text(52, 76, 0.5f, C_DIM, "No options for this tool");
		for (int i = 0; i < n; i++)
		{
			float y = 76 + i * 22;
			if (i == optSel)
				C2D_DrawRectSolid(46, y - 3, 0.5f, 308, 20, C_SEL);
			gfx_text(56, y, 0.52f, i == optSel ? C_ACCENT : C_TEXT, "%s", opt_label(rows[i]));
			char val[48];
			opt_value(rows[i], val, sizeof(val));
			gfx_text(176, y, 0.52f, C_TEXT, "< %s >", val);
		}
		gfx_text(52, 76 + n * 22 + 6, 0.42f, C_DIM, "Up/Down: option   Left/Right: change   A/B: close");
	}
	else if (mode == MODE_CONFIRM)
	{
		C2D_DrawRectSolid(70, 80, 0.5f, 260, 80, C_PANEL);
		gfx_rect_outline(70, 80, 260, 80, 1, C_ACCENT);
		gfx_text(84, 96, 0.55f, C_TEXT, "%s", confirmMsg);
		gfx_text(84, 132, 0.5f, C_DIM, "A: yes    B: no");
	}
}

// ---- main ----------------------------------------------------------------------------

int main(int argc, char* argv[])
{
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(8192);
	C2D_Prepare();

	C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget* bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	if (!gfx_init() || !doc_new(&doc, 32, 32))
		svcBreak(USERBREAK_PANIC);
	view_fit();
	after_doc_change();

	while (running && aptMainLoop())
	{
		hidScanInput();
		u32 kDown = hidKeysDown(), kHeld = hidKeysHeld(), kUp = hidKeysUp();
		touchPosition touch;
		hidTouchRead(&touch);

		if (mode == MODE_DRAW)
			update_draw(kDown, kHeld, kUp, touch);
		else if (mode == MODE_PICKER)
		{
			PickResult r = picker_update(kDown, kHeld, touch);
			if (r != PICK_OPEN)
			{
				if (r == PICK_OK && picker_color() != doc.palette[color])
				{
					hist_begin(&doc);
					hist_commit();
					doc.palette[color] = picker_color();
					doc.dirty = true;
					after_doc_change();
				}
				mode = MODE_DRAW;
				touchTarget = (kHeld & KEY_TOUCH) ? TOUCH_STRIP : TOUCH_NONE; // don't draw with the closing tap
			}
		}
		else if (mode == MODE_OPTIONS)
			update_options(kDown);
		else
			update_menus(kDown);

		if (statusTimer > 0)
			statusTimer--;
		gfx_canvas_flush();

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		gfx_frame_begin();

		C2D_TargetClear(top, C_BG_TOP);
		C2D_SceneBegin(top);
		draw_top();

		C2D_TargetClear(bottom, C_BG_BOT);
		C2D_SceneBegin(bottom);
		draw_bottom();

		C3D_FrameEnd(0);
	}

	doc_free(&doc);
	hist_clear();
	gfx_fini();
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
