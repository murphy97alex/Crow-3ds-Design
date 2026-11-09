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
#include "export_png.h"
#include "import.h"
#include "pxd.h"
#include "select.h"
#include "stamps.h"
#include "tools.h"

#define APP_NAME "Pixel UI Designer"
#define SAVE_ROOT "sdmc:/3ds/pixel-ui-designer"
#define SAVE_DIR SAVE_ROOT "/projects"
#define STAMP_DIR SAVE_ROOT "/stamps"
#define EXPORT_DIR SAVE_ROOT "/export"
#define IMPORT_DIR SAVE_ROOT "/import"
#define AUTOSAVE_PATH SAVE_DIR "/_autosave.pxd"

// Bottom screen layout
#define BOT_W 320
#define BOT_H 240
#define SW_COLS 16
#define SW_W 20
#define SW_H 11
#define RECENT_COUNT 8
#define RECENT_W (BOT_W / RECENT_COUNT)
#define STRIP_H (SW_H * 3)  // palette strip (2 rows of swatches + recent colors) at the top
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

typedef enum { TOOL_PENCIL, TOOL_ERASER, TOOL_FILL, TOOL_SHADE, TOOL_LINE, TOOL_RECT, TOOL_ELLIPSE, TOOL_SELECT, TOOL_STAMP, TOOL_EYEDROPPER, TOOL_MIRROR, TOOL_COUNT } Tool;
static const char* TOOL_NAMES[TOOL_COUNT] = { "Pencil", "Eraser", "Fill", "Shade", "Line", "Rectangle", "Ellipse", "Select", "Stamp", "Eyedropper", "Mirror" };

static bool is_shape(Tool t) { return t == TOOL_LINE || t == TOOL_RECT || t == TOOL_ELLIPSE; }

// per-tool options
static Brush brushes[TOOL_COUNT] = {
	[TOOL_PENCIL] = { 1, BRUSH_SQUARE, false }, [TOOL_MIRROR] = { 1, BRUSH_SQUARE, false },
	[TOOL_ERASER] = { 3, BRUSH_SQUARE, false },
	[TOOL_LINE] = { 1, BRUSH_SQUARE, false },   [TOOL_RECT] = { 1, BRUSH_SQUARE, false },
	[TOOL_ELLIPSE] = { 1, BRUSH_SQUARE, false },
	[TOOL_SHADE] = { 3, BRUSH_ROUND, false },
};
static bool shapeFilled[TOOL_COUNT];
enum { FILL_AREA, FILL_REPLACE, FILL_GRADIENT, FILL_MODE_COUNT };
static int fillMode;
static const char* FILL_NAMES[FILL_MODE_COUNT] = { "Connected area", "Replace color", "Gradient (drag)" };
static bool shadeDarker;               // Shade: false = lighter, true = darker
enum { SELMODE_RECT, SELMODE_WAND, SELMODE_LASSO, SELMODE_COUNT };
static int selMode;
static bool wandAll;                   // magic wand: all pixels of the color, not just connected
static const char* SELMODE_NAMES[SELMODE_COUNT] = { "Rectangle", "Magic wand", "Lasso" };
static Mirror mirrorMode = MIRROR_X;   // axis for the Mirror tool
static bool mirrorOn = false;          // applies to every drawing tool
static bool stampDragSize;             // Stamp: false = original size, true = drag to size

static const char* SHAPE_NAMES[BRUSH_SHAPE_COUNT] = { "Square", "Round", "Dither" };
static const char* MIRROR_NAMES[MIRROR_COUNT] = { "Off", "Left-right", "Top-bottom", "Both" };

// options panel rows
typedef enum {
	OPT_SIZE, OPT_SHAPE, OPT_PIXPERF, OPT_STYLE, OPT_FILLMODE, OPT_MIRROR, OPT_MIRRORON, OPT_STAMP, OPT_STAMPSIZE,
	OPT_SHADEDIR, OPT_SELMODE, OPT_WANDALL,
	// actions (A runs them)
	OPT_ACT_FIRST, OPT_SELALL = OPT_ACT_FIRST, OPT_DESELECT, OPT_COPY, OPT_CUT, OPT_PASTE, OPT_DELETE,
	OPT_FLIPH, OPT_FLIPV, OPT_ROTATE, OPT_OUTLINE, OPT_SAVESTAMP, OPT_CROP,
} OptKind;
#define OPT_MAX_ROWS 16

// stamps
static char stampNames[STAMP_MAX][DOC_NAME_MAX];
static int stampCount, stampSel;
static uint8_t* stampPx;               // stamp being placed (original pixels)
static int stampW, stampH;
static int optSel;
static Tool optTool;   // tool whose options panel is open
static int listPos;    // D-pad cursor in the tool list (Mirror is a settings entry, not a tool)

// ---- app state ---------------------------------------------------------------

typedef enum {
	MODE_DRAW, MODE_MENU, MODE_NEW, MODE_OPEN, MODE_CONFIRM, MODE_PICKER, MODE_OPTIONS, MODE_EXPORT, MODE_SHIFT,
	MODE_LAYERS, MODE_LAYER_ACT, MODE_ANCHOR, MODE_IMPORT, MODE_IMPORT_AS, MODE_COLORS, MODE_RAMP, MODE_ADJUST, MODE_VIEW,
} Mode;
typedef enum { ACT_NONE, ACT_NEW, ACT_OPEN, ACT_EXIT, ACT_OVERWRITE, ACT_IMPORT_PROJECT } Action;

static Doc doc;
static Mode mode = MODE_DRAW;
static Tool tool = TOOL_PENCIL, lastTool = TOOL_ERASER;
static Tool eraserReturn = TOOL_PENCIL;  // where the B eraser toggle goes back to
static uint32_t curColor;      // the color you draw with (0xRRGGBBAA)
static int curSwatch = 0;      // swatch it came from, or -1
static uint8_t strokeValue;    // pixel value for curColor, fixed for the current stroke
static int zoomIdx;
static float viewX, viewY;     // canvas coords at the view's top-left corner
static bool gridOn = true;
static bool previewMode = false;
static bool running = true;

static const float ZOOMS[] = { 0.25f, 0.5f, 1, 2, 3, 4, 6, 8, 12, 16, 24 };
#define ZOOM_FIT_MIN 2  // index of 1x: "fit to screen" never starts below 1x
#define ZOOM_COUNT (int)(sizeof(ZOOMS) / sizeof(ZOOMS[0]))

// touch / stroke
enum { TOUCH_NONE, TOUCH_CANVAS, TOUCH_STRIP };
static int touchTarget = TOUCH_NONE;
static int startCX, startCY;   // where the current stroke/shape began
static bool strokeActive;
enum { SEL_IDLE, SEL_DRAWING, SEL_MOVING, SEL_SIZING, SEL_LASSO };
#define LASSO_MAX 1024
static int lassoPts[LASSO_MAX * 2], lassoCount;
static uint8_t shadeMap[256];
static uint32_t recent[RECENT_COUNT];
static int recentCount;
static int selLastCX, selLastCY;  // last stylus position (the release frame reads 0,0)
static int selDrag = SEL_IDLE;
static int moveOffX, moveOffY;  // stylus offset inside the floating pixels
static int shiftX, shiftY, shiftLastCX, shiftLastCY;

// menus
static int menuSel, newSel, openSel, openCount;
static char openFiles[128][DOC_NAME_MAX + 8];
static Action confirmAction;
static char confirmMsg[96];
static int pendingNewW, pendingNewH;
static char pendingName[DOC_NAME_MAX];

static char status[96];
static int statusTimer;

// layers menu
static int layerCursor;            // index into doc.layers
static int layerActSel;
// canvas size
static int resizeW, resizeH, anchorX = 1, anchorY = 1;
// import
static char importFiles[64][DOC_NAME_MAX + 8];
static int importCount, importSel, importAsSel;
// colors
static int colorsSel;
static int adjSel, adjHue, adjSat, adjBri;
static bool adjInvert;
static uint8_t* adjOrig;
static int adjCount;
// view
static bool flipPreview, tilePreview;
static int hudGrid;                // 0, 8, 16, 32
static int isoGrid;                // 0, 8, 16
static int autosaveMin = 2;        // 0 = off
static int viewSel;
static unsigned autosaveFrames;

static void scan_imports(void);
static void import_as(int how);

static void set_status(const char* msg)
{
	snprintf(status, sizeof(status), "%s", msg);
	statusTimer = 180;
}

// ---- view --------------------------------------------------------------------

static float zoom(void) { return ZOOMS[zoomIdx]; }

static const char* zoom_label(void)
{
	static char buf[8];
	float z = zoom();
	if (z < 1) snprintf(buf, sizeof(buf), "1/%dx", (int)(1 / z + 0.5f));
	else snprintf(buf, sizeof(buf), "%dx", (int)z);
	return buf;
}

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
	zoomIdx = ZOOM_FIT_MIN;
	for (int i = ZOOM_COUNT - 1; i >= ZOOM_FIT_MIN; i--)
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

static void after_doc_change(void);

static void refresh_stamps(void)
{
	stampCount = stamp_list(STAMP_DIR, stampNames, STAMP_MAX);
	if (stampSel >= stampCount) stampSel = stampCount ? stampCount - 1 : 0;
}

// Drop any floating pixels into the canvas (one undo step).
static void finish_floating(void)
{
	if (sel_floating())
	{
		sel_commit(&doc);
		after_doc_change();
	}
}

static void set_tool(Tool t)
{
	if (t == tool)
		return;
	if (tool == TOOL_SELECT)
		finish_floating();          // the selection itself stays, like other editors
	if (tool == TOOL_STAMP || t == TOOL_STAMP)
	{
		sel_clear(&doc);            // stamps place through the floating layer
		after_doc_change();
	}
	if (t == TOOL_STAMP)
		refresh_stamps();
	lastTool = tool;
	tool = t;
	listPos = t;
}

static void eyedrop(int cx, int cy)
{
	if (cx < 0 || cy < 0 || cx >= doc.w || cy >= doc.h)
		return;
	// the color you see: topmost visible layer with a pixel here
	uint8_t v = PIX_TRANSPARENT;
	for (int i = doc.layerCount - 1; i >= 0 && !v; i--)
		if (!doc.layers[i].isFolder && doc_layer_visible(&doc, i))
			v = doc.layers[i].cels[doc.curFrame][cy * doc.w + cx];
	if (v != PIX_TRANSPARENT)
	{
		curColor = doc_color(&doc, v);
		curSwatch = -1;
		for (int i = 0; i < DOC_PALETTE_SIZE; i++)
			if (doc.swatches[i] == curColor) { curSwatch = i; break; }
	}
}

static void after_doc_change(void)
{
	gfx_canvas_rebuild(&doc);
}

static void push_recent(uint32_t c)
{
	int i = 0;
	while (i < recentCount && recent[i] != c) i++;
	if (i == recentCount && recentCount < RECENT_COUNT) recentCount++;
	if (i >= RECENT_COUNT) i = RECENT_COUNT - 1;
	memmove(&recent[1], &recent[0], sizeof(uint32_t) * i);
	recent[0] = c;
}

static void pick_color(uint32_t c)
{
	curColor = c;
	curSwatch = -1;
	for (int i = 0; i < DOC_PALETTE_SIZE; i++)
		if (doc.swatches[i] == c) { curSwatch = i; break; }
}

// Can the current layer be edited? Explains why not.
static bool layer_editable(void)
{
	if (doc_layer_locked(&doc, doc.curLayer)) { set_status("Layer is locked (Y: layers)"); return false; }
	if (!doc_layer_visible(&doc, doc.curLayer)) { set_status("Layer is hidden (Y: layers)"); return false; }
	return true;
}

// Shading: each color that is a swatch steps to the next swatch (right = lighter).
static void build_shade_map(void)
{
	for (int i = 0; i < 256; i++) shadeMap[i] = i;
	int count = doc.colorCount;
	for (int v = 1; v <= count; v++)
		for (int k = 0; k < DOC_PALETTE_SIZE; k++)
			if (doc.swatches[k] == doc.colors[v])
			{
				int t = k + (shadeDarker ? -1 : 1);
				if (t >= 0 && t < DOC_PALETTE_SIZE)
					shadeMap[v] = doc_color_index(&doc, doc.swatches[t], false);
				break;
			}
}

// ---- tool options ----------------------------------------------------------------

static int opt_rows(Tool t, OptKind* rows)
{
	int n = 0;
	switch (t)
	{
	case TOOL_PENCIL:
		rows[n++] = OPT_SIZE; rows[n++] = OPT_SHAPE; rows[n++] = OPT_PIXPERF; break;
	case TOOL_MIRROR:
		rows[n++] = OPT_MIRRORON; rows[n++] = OPT_MIRROR; break;
	case TOOL_ERASER:
		rows[n++] = OPT_SIZE; rows[n++] = OPT_SHAPE; break;
	case TOOL_FILL:
		rows[n++] = OPT_FILLMODE; break;
	case TOOL_SHADE:
		rows[n++] = OPT_SHADEDIR; rows[n++] = OPT_SIZE; rows[n++] = OPT_SHAPE; break;
	case TOOL_LINE:
		rows[n++] = OPT_SIZE; rows[n++] = OPT_SHAPE; break;
	case TOOL_RECT:
	case TOOL_ELLIPSE:
		rows[n++] = OPT_STYLE; rows[n++] = OPT_SIZE; break;
	case TOOL_SELECT:
	{
		rows[n++] = OPT_SELMODE;
		if (selMode == SELMODE_WAND) rows[n++] = OPT_WANDALL;
		static const OptKind acts[] = { OPT_SELALL, OPT_DESELECT, OPT_COPY, OPT_CUT, OPT_PASTE, OPT_DELETE,
		                                OPT_FLIPH, OPT_FLIPV, OPT_ROTATE, OPT_OUTLINE, OPT_CROP, OPT_SAVESTAMP };
		for (size_t i = 0; i < sizeof(acts) / sizeof(acts[0]); i++)
			rows[n++] = acts[i];
		return n;
	}
	case TOOL_STAMP:
		rows[n++] = OPT_STAMP; rows[n++] = OPT_STAMPSIZE;
		return n;
	default:
		break;
	}
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
	case OPT_MIRROR: return "Axis";
	case OPT_MIRRORON: return "Mirror";
	case OPT_STAMPSIZE: return "Size";
	case OPT_STAMP: return "Stamp";
	case OPT_SELALL: return "Select all";
	case OPT_DESELECT: return "Deselect";
	case OPT_COPY: return "Copy";
	case OPT_CUT: return "Cut";
	case OPT_PASTE: return "Paste";
	case OPT_DELETE: return "Delete";
	case OPT_FLIPH: return "Flip left-right";
	case OPT_FLIPV: return "Flip top-bottom";
	case OPT_ROTATE: return "Rotate 90\xC2\xB0";
	case OPT_OUTLINE: return "Outline (current color)";
	case OPT_SAVESTAMP: return "Save as stamp...";
	case OPT_CROP: return "Crop canvas to selection";
	case OPT_SHADEDIR: return "Direction";
	case OPT_SELMODE: return "Mode";
	case OPT_WANDALL: return "Wand";
	}
	return "";
}

static void opt_value(OptKind k, char* out, size_t n)
{
	Brush* b = &brushes[optTool];
	switch (k)
	{
	case OPT_SIZE: snprintf(out, n, "%d px", b->size); break;
	case OPT_SHAPE: snprintf(out, n, "%s", SHAPE_NAMES[b->shape]); break;
	case OPT_PIXPERF: snprintf(out, n, "%s%s", b->pixelPerfect ? "On" : "Off", b->pixelPerfect && b->size > 1 ? " (size 1 only)" : ""); break;
	case OPT_STYLE: snprintf(out, n, "%s", shapeFilled[optTool] ? "Filled" : "Outline"); break;
	case OPT_FILLMODE: snprintf(out, n, "%s", FILL_NAMES[fillMode]); break;
	case OPT_SHADEDIR: snprintf(out, n, "%s", shadeDarker ? "Darker (swatch to the left)" : "Lighter (swatch to the right)"); break;
	case OPT_SELMODE: snprintf(out, n, "%s", SELMODE_NAMES[selMode]); break;
	case OPT_WANDALL: snprintf(out, n, "%s", wandAll ? "All of that color" : "Connected only"); break;
	case OPT_MIRROR: snprintf(out, n, "%s", MIRROR_NAMES[mirrorMode]); break;
	case OPT_MIRRORON: snprintf(out, n, "%s", mirrorOn ? "On" : "Off"); break;
	case OPT_STAMPSIZE: snprintf(out, n, "%s", stampDragSize ? "Drag to size" : "Original"); break;
	case OPT_STAMP:
		if (stampCount) snprintf(out, n, "%s  (%d/%d)", stampNames[stampSel], stampSel + 1, stampCount);
		else snprintf(out, n, "none saved yet");
		break;
	default: out[0] = 0; break;
	}
}

static void opt_change(OptKind k, int dir)
{
	Brush* b = &brushes[optTool];
	switch (k)
	{
	case OPT_SIZE:
		b->size += dir;
		if (b->size < 1) b->size = 1;
		if (b->size > BRUSH_MAX) b->size = BRUSH_MAX;
		break;
	case OPT_SHAPE:
	{
		int count = optTool == TOOL_LINE ? 2 : BRUSH_SHAPE_COUNT; // no dither lines
		b->shape = (b->shape + count + dir) % count;
		break;
	}
	case OPT_PIXPERF: b->pixelPerfect = !b->pixelPerfect; break;
	case OPT_STYLE: shapeFilled[optTool] = !shapeFilled[optTool]; break;
	case OPT_FILLMODE: fillMode = (fillMode + FILL_MODE_COUNT + dir) % FILL_MODE_COUNT; break;
	case OPT_SHADEDIR: shadeDarker = !shadeDarker; break;
	case OPT_SELMODE: selMode = (selMode + SELMODE_COUNT + dir) % SELMODE_COUNT; break;
	case OPT_WANDALL: wandAll = !wandAll; break;
	case OPT_MIRROR: // Left-right, Top-bottom, Both ("Off" is the On/Off row)
		mirrorMode = (Mirror)((mirrorMode - 1 + 3 + dir) % 3 + 1);
		break;
	case OPT_STAMP: if (stampCount) stampSel = (stampSel + stampCount + dir) % stampCount; break;
	case OPT_MIRRORON: mirrorOn = !mirrorOn; break;
	case OPT_STAMPSIZE: stampDragSize = !stampDragSize; break;
	default: break;
	}
}

static bool ask_text(const char* hint, const char* init, char* out, size_t n);
static void ensure_dirs(void);

static void save_stamp(void)
{
	uint8_t* px;
	int w, h;
	if (!sel_get_pixels(&doc, &px, &w, &h))
	{
		set_status("Select something first");
		return;
	}
	char name[DOC_NAME_MAX];
	if (ask_text("Stamp name", "stamp", name, sizeof(name)))
	{
		ensure_dirs();
		const char* err = stamp_save(STAMP_DIR, name, px, w, h, &doc);
		char msg[96];
		snprintf(msg, sizeof(msg), err ? "Stamp save failed: %s" : "Saved stamp %s", err ? err : name);
		set_status(msg);
		refresh_stamps();
	}
	free(px);
}

static void run_option_action(OptKind k)
{
	bool needSel = k == OPT_COPY || k == OPT_CUT || k == OPT_DELETE || k == OPT_SAVESTAMP || k == OPT_CROP;
	if (needSel && !sel_active())
	{
		set_status("Select something first");
		return;
	}
	switch (k)
	{
	case OPT_SELALL: sel_all(&doc); break;
	case OPT_DESELECT: sel_clear(&doc); break;
	case OPT_COPY: sel_copy(&doc); set_status("Copied"); break;
	case OPT_CUT: sel_cut(&doc); set_status("Cut"); break;
	case OPT_PASTE:
		if (!sel_has_clip()) { set_status("Nothing copied yet"); break; }
		if (sel_active()) { Rect r = sel_rect(); sel_paste(&doc, r.x, r.y); }
		else sel_paste(&doc, viewX < 0 ? 0 : (int)viewX, viewY < 0 ? 0 : (int)viewY);
		set_status("Pasted - drag to move, B to drop");
		break;
	case OPT_DELETE: sel_delete(&doc); break;
	case OPT_FLIPH: sel_flip(&doc, true); break;
	case OPT_FLIPV: sel_flip(&doc, false); break;
	case OPT_ROTATE: sel_rotate(&doc); break;
	case OPT_OUTLINE: sel_outline(&doc, doc_color_index(&doc, curColor, !sel_floating())); break;
	case OPT_SAVESTAMP: save_stamp(); break;
	case OPT_CROP:
	{
		finish_floating();
		Rect r = sel_rect();
		hist_begin(&doc);
		sel_clear(&doc);
		if (doc_crop(&doc, r.x, r.y, r.w, r.h)) { hist_commit(); doc.dirty = true; view_fit(); }
		else hist_abort();
		break;
	}
	default: break;
	}
	after_doc_change();
}

static void view_fit(void);

static void update_options(u32 kDown)
{
	OptKind rows[OPT_MAX_ROWS];
	int n = opt_rows(optTool, rows);
	if (optSel >= n) optSel = n ? n - 1 : 0;
	if (kDown & KEY_B) { mode = MODE_DRAW; return; }
	if (kDown & KEY_A)
	{
		mode = MODE_DRAW;
		if (n && rows[optSel] >= OPT_ACT_FIRST)
			run_option_action(rows[optSel]);
		return;
	}
	if (n == 0) return;
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
	case TOOL_MIRROR:
		snprintf(out, n, "%s", mirrorOn ? MIRROR_NAMES[mirrorMode] : "Off"); break;
	case TOOL_ERASER:
	case TOOL_LINE:
		snprintf(out, n, "%d %s", b->size, SHAPE_NAMES[b->shape]); break;
	case TOOL_RECT:
	case TOOL_ELLIPSE:
		if (shapeFilled[t]) snprintf(out, n, "Filled");
		else snprintf(out, n, "Outline %d", b->size);
		break;
	case TOOL_FILL: snprintf(out, n, "%s", fillMode == FILL_AREA ? "Area" : fillMode == FILL_REPLACE ? "Replace" : "Gradient"); break;
	case TOOL_SHADE: snprintf(out, n, "%s %d", shadeDarker ? "Darker" : "Lighter", b->size); break;
	case TOOL_SELECT:
		if (sel_active()) { Rect r = sel_rect(); snprintf(out, n, "%s %dx%d", SELMODE_NAMES[selMode], r.w, r.h); }
		else snprintf(out, n, "%s", SELMODE_NAMES[selMode]);
		break;
	case TOOL_STAMP: snprintf(out, n, "%s%s", stampCount ? stampNames[stampSel] : "none", stampDragSize ? " (drag)" : ""); break;
	default: out[0] = 0; break;
	}
}

// ---- files ---------------------------------------------------------------------

static void ensure_dirs(void)
{
	mkdir("sdmc:/3ds", 0777);
	mkdir(SAVE_ROOT, 0777);
	mkdir(SAVE_DIR, 0777);
	mkdir(STAMP_DIR, 0777);
	mkdir(EXPORT_DIR, 0777);
	mkdir(IMPORT_DIR, 0777);
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
	if (strcmp(name, "_autosave")) remove(AUTOSAVE_PATH);
	autosaveFrames = 0;
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

// Asks for a file name. Returns false if cancelled.
static bool ask_text(const char* hint, const char* init, char* out, size_t n)
{
	SwkbdState kb;
	char buf[DOC_NAME_MAX] = {0};
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, DOC_NAME_MAX - 1);
	swkbdSetHintText(&kb, hint);
	swkbdSetInitialText(&kb, init);
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

static bool ask_name(char* out, size_t n)
{
	return ask_text("Project name", doc.name[0] ? doc.name : "untitled", out, n);
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
	sel_reset();
	const char* err = pxd_load(&doc, path);
	if (err)
	{
		char msg[96];
		snprintf(msg, sizeof(msg), "Open failed: %s", err);
		set_status(msg);
		return;
	}
	snprintf(doc.name, sizeof(doc.name), "%s", name);
	if (strcmp(name, "_autosave")) remove(AUTOSAVE_PATH);
	hist_clear();
	view_fit();
	after_doc_change();
	set_status("Opened");
}

static void new_canvas(int w, int h)
{
	sel_reset();
	if (!doc_new(&doc, w, h))
	{
		set_status("Couldn't create canvas");
		return;
	}
	curColor = doc.swatches[0];
	curSwatch = 0;
	remove(AUTOSAVE_PATH);
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
	case ACT_EXIT: remove(AUTOSAVE_PATH); running = false; break;
	case ACT_OVERWRITE: save_as_name(pendingName); break;
	case ACT_IMPORT_PROJECT: import_as(0); break;
	default: break;
	}
}

// ---- menus ---------------------------------------------------------------------

static const char* MENU_ITEMS[] = { "Back to canvas", "New canvas", "Save", "Save as...", "Open...",
                                    "Import PNG...", "Export PNG...", "Canvas size...", "Shift layer (wrap)...",
                                    "Colors...", "View...", "Linked swatches", "Exit" };
#define MENU_COUNT 13
#define MENU_LINKED 11

// ---- PNG export ------------------------------------------------------------------

typedef struct { int mult, fixed; } ExportSize;
static const ExportSize EXPORT_SIZES[] = {
	{ 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 6, 0 }, { 8, 0 },
	{ 0, 16 }, { 0, 24 }, { 0, 32 }, { 0, 48 }, { 0, 64 }, { 0, 128 }, { 0, 256 },
};
#define EXPORT_SIZE_COUNT (int)(sizeof(EXPORT_SIZES) / sizeof(EXPORT_SIZES[0]))
#define EXPORT_ROWS (EXPORT_SIZE_COUNT + 2)  // "Recommended" row + sizes + "Export" row
static bool exportOn[EXPORT_SIZE_COUNT];
static int exportSel;

static void export_recommended(void)
{
	for (int i = 0; i < EXPORT_SIZE_COUNT; i++)
	{
		int m = EXPORT_SIZES[i].mult;
		exportOn[i] = m == 1 || m == 2 || m == 4 || m == 8;
	}
}

static bool export_is_recommended(void)
{
	for (int i = 0; i < EXPORT_SIZE_COUNT; i++)
	{
		int m = EXPORT_SIZES[i].mult;
		if (exportOn[i] != (m == 1 || m == 2 || m == 4 || m == 8)) return false;
	}
	return true;
}

// Output size for an entry; returns false if too big. *even = whole-number scale.
static bool export_dims(int i, int* w, int* h, bool* even)
{
	const ExportSize* e = &EXPORT_SIZES[i];
	if (e->mult)
	{
		*w = doc.w * e->mult;
		*h = doc.h * e->mult;
		*even = true;
	}
	else
	{
		int longest = doc.w > doc.h ? doc.w : doc.h;
		*w = (doc.w * e->fixed + longest / 2) / longest;
		*h = (doc.h * e->fixed + longest / 2) / longest;
		if (*w < 1) *w = 1;
		if (*h < 1) *h = 1;
		*even = e->fixed % longest == 0;
	}
	return *w <= PNG_MAX_DIM && *h <= PNG_MAX_DIM;
}

static void run_export(void)
{
	ensure_dirs();
	const char* base = doc.name[0] ? doc.name : "untitled";
	int done = 0, failed = 0;
	for (int i = 0; i < EXPORT_SIZE_COUNT; i++)
	{
		int w, h;
		bool even;
		if (!exportOn[i] || !export_dims(i, &w, &h, &even)) continue;
		char path[200];
		snprintf(path, sizeof(path), EXPORT_DIR "/%s_%dx%d.png", base, w, h);
		if (png_export(&doc, doc.curFrame, w, h, path)) failed++;
		else done++;
	}
	char msg[96];
	if (failed) snprintf(msg, sizeof(msg), "Exported %d PNG, %d failed", done, failed);
	else if (done) snprintf(msg, sizeof(msg), "Exported %d PNG to /3ds/pixel-ui-designer/export", done);
	else snprintf(msg, sizeof(msg), "No sizes ticked");
	set_status(msg);
}

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
				if (doc.name[0] && strcmp(doc.name, "_autosave")) { save_as_name(doc.name); mode = MODE_DRAW; }
				else save_as();
				break;
			case 3: save_as(); break;
			case 4: scan_projects(); openSel = 0; mode = MODE_OPEN; break;
			case 5: scan_imports(); importSel = 0; mode = MODE_IMPORT; break;
			case 6: mode = MODE_EXPORT; exportSel = 0; break;
			case 7:
			{
				int w = ask_number("New width (1-320)", doc.w, DOC_MAX_W);
				int h = w ? ask_number("New height (1-240)", doc.h, DOC_MAX_H) : 0;
				if (w && h) { resizeW = w; resizeH = h; mode = MODE_ANCHOR; }
				break;
			}
			case 8:
				if (!layer_editable()) break;
				shift_begin(&doc);
				shiftX = shiftY = 0;
				mode = MODE_SHIFT;
				break;
			case 9: colorsSel = 0; mode = MODE_COLORS; break;
			case 10: viewSel = 0; mode = MODE_VIEW; break;
			case MENU_LINKED:
				doc.linkedSwatches = !doc.linkedSwatches;
				doc.dirty = true;
				set_status(doc.linkedSwatches ? "Linked: editing a swatch recolors the drawing"
				                              : "Unlinked: swatches are just saved colors");
				break;
			case 12:
				if (doc.dirty) confirm(ACT_EXIT, "Exit without saving?");
				else { remove(AUTOSAVE_PATH); running = false; }
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
	case MODE_EXPORT:
		list_nav(kDown, &exportSel, EXPORT_ROWS);
		if (kDown & KEY_B) mode = MODE_MENU;
		if (kDown & KEY_A)
		{
			if (exportSel == 0) export_recommended();
			else if (exportSel == EXPORT_ROWS - 1) { run_export(); mode = MODE_DRAW; }
			else exportOn[exportSel - 1] = !exportOn[exportSel - 1];
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

// ---- layers menu -----------------------------------------------------------------

static const char* LAYER_ACTS[] = { "New layer", "New folder", "Duplicate", "Delete", "Move up", "Move down",
                                    "Merge down", "Merge visible", "Show / hide", "Lock / unlock", "Rename...",
                                    "Collapse / expand folder" };
#define LAYER_ACT_COUNT 12

// Layer entries in display order (top first), skipping members of collapsed folders.
static int layer_rows(int* rows)
{
	int n = 0;
	for (int i = doc.layerCount - 1; i >= 0; i--)
	{
		const Layer* L = &doc.layers[i];
		if (L->folder >= 0)
		{
			int f = doc_layer_index(&doc, L->folder);
			if (f >= 0 && doc.layers[f].collapsed) continue;
		}
		rows[n++] = i;
	}
	return n;
}

static void open_layers(void)
{
	finish_floating();
	layerCursor = doc.curLayer;
	mode = MODE_LAYERS;
}

static void run_layer_action(int a)
{
	int i = layerCursor;
	Layer* L = &doc.layers[i];
	int r = -1;
	bool structural = a <= 7;
	if (structural) hist_begin(&doc);
	switch (a)
	{
	case 0: doc_set_current_layer(&doc, L->isFolder ? doc.curLayer : i); r = doc_layer_add(&doc, false); break;
	case 1: r = doc_layer_add(&doc, true); break;
	case 2: r = doc_layer_duplicate(&doc, i); break;
	case 3: r = doc_layer_delete(&doc, i) ? doc.curLayer : -1; break;
	case 4: r = doc_layer_move(&doc, i, +1); break;
	case 5: r = doc_layer_move(&doc, i, -1); break;
	case 6: r = doc_layer_merge_down(&doc, i) ? doc.curLayer : -1; break;
	case 7: r = doc_merge_visible(&doc) ? doc.curLayer : -1; break;
	case 8: L->visible = !L->visible; doc.dirty = true; break;
	case 9: L->locked = !L->locked; doc.dirty = true; break;
	case 10:
	{
		char nm[DOC_NAME_MAX];
		if (ask_text("Layer name", L->name, nm, LAYER_NAME_MAX)) { snprintf(L->name, LAYER_NAME_MAX, "%.15s", nm); doc.dirty = true; }
		break;
	}
	case 11:
		if (L->isFolder) { L->collapsed = !L->collapsed; doc.dirty = true; }
		else set_status("Not a folder");
		break;
	}
	if (structural)
	{
		if (r >= 0) { hist_commit(); doc.dirty = true; layerCursor = r; }
		else
		{
			hist_abort();
			static const char* why[] = { "Too many layers (16 max)", "Too many layers (16 max)", "Can't duplicate a folder",
			                             "Can't delete the last layer", "Already at the top", "Already at the bottom",
			                             "Nothing to merge into below", "Need 2+ visible layers" };
			set_status(why[a]);
		}
	}
	after_doc_change();
}

static void update_layers(u32 kDown)
{
	int rows[DOC_MAX_LAYERS];
	int n = layer_rows(rows), pos = 0;
	for (int k = 0; k < n; k++) if (rows[k] == layerCursor) pos = k;
	if (kDown & (KEY_B | KEY_Y)) { mode = MODE_DRAW; return; }
	if (kDown & KEY_DUP) pos = (pos + n - 1) % n;
	if (kDown & KEY_DDOWN) pos = (pos + 1) % n;
	layerCursor = rows[pos];
	doc_set_current_layer(&doc, layerCursor);   // folders keep the previous drawing layer
	Layer* L = &doc.layers[layerCursor];
	if (kDown & (KEY_DLEFT | KEY_DRIGHT))
	{
		int o = L->opacity + ((kDown & KEY_DRIGHT) ? 26 : -26);
		L->opacity = o < 0 ? 0 : o > 255 ? 255 : o;
		doc.dirty = true;
		after_doc_change();
	}
	if (kDown & KEY_X) { L->visible = !L->visible; doc.dirty = true; after_doc_change(); }
	if (kDown & KEY_R) run_layer_action(4);
	if (kDown & KEY_L) run_layer_action(5);
	if (kDown & KEY_A) { layerActSel = 0; mode = MODE_LAYER_ACT; }
}

// ---- import --------------------------------------------------------------------

static void scan_imports(void)
{
	ensure_dirs();
	importCount = 0;
	DIR* dir = opendir(IMPORT_DIR);
	if (!dir) return;
	struct dirent* e;
	while ((e = readdir(dir)) && importCount < 64)
	{
		size_t n = strlen(e->d_name);
		if (n > 4 && n < sizeof(importFiles[0]) && !strcasecmp(e->d_name + n - 4, ".png"))
			snprintf(importFiles[importCount++], sizeof(importFiles[0]), "%s", e->d_name);
	}
	closedir(dir);
	qsort(importFiles, importCount, sizeof(importFiles[0]), (int (*)(const void*, const void*))strcasecmp);
}

static const char* IMPORT_AS[] = { "As new project", "As new layer", "As stamp", "As swatches (palette)" };

// how: 0 project, 1 layer, 2 stamp, 3 swatches
static void import_as(int how)
{
	char path[200];
	snprintf(path, sizeof(path), IMPORT_DIR "/%s", importFiles[importSel]);
	uint32_t* rgba;
	int w, h;
	bool shrunk = false;
	// shrink while reading: big images never sit in memory at full size
	int maxW = how == 1 ? doc.w : DOC_MAX_W, maxH = how == 1 ? doc.h : DOC_MAX_H;
	const char* err = import_read_png(path, maxW, maxH, &rgba, &w, &h, &shrunk);
	if (err) { set_status(err); return; }
	char base[DOC_NAME_MAX];
	snprintf(base, sizeof(base), "%s", importFiles[importSel]);
	char* dot = strrchr(base, '.');
	if (dot) *dot = 0;
	for (char* c = base; *c; c++)
		if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-')) *c = '_';
	if (how == 3)
	{
		uint32_t cols[DOC_PALETTE_SIZE];
		int n = import_palette(rgba, w, h, cols, DOC_PALETTE_SIZE);
		memcpy(doc.swatches, cols, n * sizeof(uint32_t));
		doc.dirty = true;
		char msg[64];
		snprintf(msg, sizeof(msg), n ? "Set %d swatches from the image" : "No colors found", n);
		set_status(msg);
	}
	else if (how == 0)
	{
		{
			sel_reset();
			doc_new(&doc, w, h);
			uint8_t* px = import_to_indexed(&doc, rgba, w, h);
			if (px) { memcpy(doc.layers[0].cels[0], px, (size_t)w * h); free(px); }
			snprintf(doc.name, sizeof(doc.name), "%s", base);
			doc.dirty = true;
			remove(AUTOSAVE_PATH);
			view_fit();
			set_status(shrunk ? "Imported (shrunk to fit 320x240)" : "Imported as new project");
		}
	}
	else if (how == 1)
	{
		hist_begin(&doc);
		int li = doc_layer_add(&doc, false);
		if (li >= 0)
		{
			uint8_t* px = import_to_indexed(&doc, rgba, w, h);
			if (px)
			{
				for (int y = 0; y < h; y++)
					memcpy(doc.layers[li].cels[doc.curFrame] + y * doc.w, px + y * w, w);
				free(px);
			}
			snprintf(doc.layers[li].name, LAYER_NAME_MAX, "%.15s", base);
			hist_commit();
			doc.dirty = true;
			set_status(shrunk ? "Imported as layer (shrunk to fit)" : "Imported as new layer");
		}
		else { hist_abort(); set_status("Too many layers (16 max)"); }
	}
	else
	{
		Doc tmp = {0};   // built by hand: doc_new() would reset the undo history
		if ((tmp.layers[0].cels[0] = calloc((size_t)w * h, 1)))
		{
			tmp.w = w;
			tmp.h = h;
			tmp.frameCount = 1;
			tmp.layerCount = 1;
			tmp.layers[0].visible = true;
			tmp.layers[0].opacity = 255;
			uint8_t* px = import_to_indexed(&tmp, rgba, w, h);
			if (px)
			{
				ensure_dirs();
				const char* e2 = stamp_save(STAMP_DIR, base, px, w, h, &tmp);
				set_status(e2 ? e2 : "Saved as stamp (pick it with the Stamp tool)");
				free(px);
			}
			doc_free(&tmp);
			refresh_stamps();
		}
	}
	free(rgba);
	after_doc_change();
}

// ---- colors ----------------------------------------------------------------------

static uint32_t adjust_color(uint32_t c)
{
	if (adjInvert)
		c = ((~c) & 0xFFFFFF00) | 0xFF;
	float h, sa, v;
	rgba_to_hsv(c, &h, &sa, &v);
	h = fmodf(h + adjHue + 360.0f, 360.0f);
	sa *= 1.0f + adjSat / 100.0f;
	v *= 1.0f + adjBri / 100.0f;
	sa = sa < 0 ? 0 : sa > 1 ? 1 : sa;
	v = v < 0 ? 0 : v > 1 ? 1 : v;
	return hsv_to_rgba(h, sa, v);
}

static void adjust_preview(void)
{
	uint8_t* px = doc_pixels(&doc);
	size_t n = (size_t)doc.w * doc.h;
	memcpy(px, adjOrig, n);
	doc.colorCount = adjCount;
	int16_t map[256];
	for (int i = 0; i < 256; i++) map[i] = -1;
	map[0] = 0;
	for (int y = 0; y < doc.h; y++)
		for (int x = 0; x < doc.w; x++)
		{
			if (sel_active() && !sel_contains(x, y)) continue;
			uint8_t v = px[y * doc.w + x];
			if (map[v] < 0) map[v] = doc_color_index(&doc, adjust_color(doc.colors[v]), false);
			px[y * doc.w + x] = map[v];
		}
	after_doc_change();
}

static void adjust_begin(void)
{
	finish_floating();
	if (!layer_editable()) { mode = MODE_DRAW; return; }
	doc_compact(&doc);  // room for new colors
	size_t n = (size_t)doc.w * doc.h;
	free(adjOrig);
	adjOrig = malloc(n);
	if (!adjOrig) { mode = MODE_DRAW; return; }
	memcpy(adjOrig, doc_pixels(&doc), n);
	adjCount = doc.colorCount;
	hist_begin(&doc);
	adjSel = adjHue = adjSat = adjBri = 0;
	adjInvert = false;
	mode = MODE_ADJUST;
}

static void adjust_end(bool keep)
{
	size_t n = (size_t)doc.w * doc.h;
	if (keep && memcmp(adjOrig, doc_pixels(&doc), n)) { hist_commit(); doc.dirty = true; }
	else
	{
		memcpy(doc_pixels(&doc), adjOrig, n);
		doc.colorCount = adjCount;
		hist_abort();
	}
	free(adjOrig);
	adjOrig = NULL;
	after_doc_change();
	mode = MODE_DRAW;
}

static void update_adjust(u32 kDown)
{
	if (kDown & KEY_A) { adjust_end(true); return; }
	if (kDown & KEY_B) { adjust_end(false); return; }
	if (kDown & KEY_DUP) adjSel = (adjSel + 3) % 4;
	if (kDown & KEY_DDOWN) adjSel = (adjSel + 1) % 4;
	int dir = (kDown & KEY_DRIGHT) ? 1 : (kDown & KEY_DLEFT) ? -1 : 0;
	if (!dir) return;
	switch (adjSel)
	{
	case 0: adjHue = (adjHue + dir * 10 + 540) % 360 - 180; break;
	case 1: adjSat += dir * 10; if (adjSat < -100) adjSat = -100; if (adjSat > 100) adjSat = 100; break;
	case 2: adjBri += dir * 10; if (adjBri < -100) adjBri = -100; if (adjBri > 100) adjBri = 100; break;
	case 3: adjInvert = !adjInvert; break;
	}
	adjust_preview();
}

// Fill the swatches between the current swatch and `end` with an even ramp.
static void make_ramp(int end)
{
	int a = curSwatch, b = end;
	if (a < 0) { set_status("Pick the start swatch first"); return; }
	if (abs(b - a) < 2) { set_status("Pick a swatch at least 2 away"); return; }
	int lo = a < b ? a : b, hi = a < b ? b : a;
	uint32_t from[DOC_PALETTE_SIZE], to[DOC_PALETTE_SIZE];
	int n = 0;
	uint32_t c0 = doc.swatches[lo], c1 = doc.swatches[hi];
	for (int k = lo + 1; k < hi; k++)
	{
		float t = (float)(k - lo) / (hi - lo);
		uint32_t c = 0xFF;
		for (int sh = 8; sh <= 24; sh += 8)
		{
			int v0 = (c0 >> sh) & 255, v1 = (c1 >> sh) & 255;
			c |= (uint32_t)lroundf(v0 + (v1 - v0) * t) << sh;
		}
		from[n] = doc.swatches[k];
		to[n++] = c;
		doc.swatches[k] = c;
	}
	if (doc.linkedSwatches)
	{
		hist_begin(&doc);
		if (doc_recolor(&doc, from, to, n)) { hist_commit(); after_doc_change(); }
		else hist_abort();
	}
	doc.dirty = true;
	char msg[64];
	snprintf(msg, sizeof(msg), "Ramp: swatches %d-%d", lo + 1, hi + 1);
	set_status(msg);
}

static const char* COLORS_ITEMS[] = { "Color ramp (between two swatches)...", "Adjust colors (hue/sat/bright/invert)..." };

// ---- view -------------------------------------------------------------------------

#define VIEW_ROWS 5
static void view_value(int row, char* out, size_t n)
{
	switch (row)
	{
	case 0: snprintf(out, n, "%s", flipPreview ? "On" : "Off"); break;
	case 1: snprintf(out, n, "%s", tilePreview ? "On (3x3)" : "Off"); break;
	case 2: if (hudGrid) snprintf(out, n, "Every %d px", hudGrid); else snprintf(out, n, "Off"); break;
	case 3: if (isoGrid) snprintf(out, n, "2:1, %d px", isoGrid); else snprintf(out, n, "Off"); break;
	case 4: if (autosaveMin) snprintf(out, n, "Every %d min", autosaveMin); else snprintf(out, n, "Off"); break;
	}
}

static void update_view(u32 kDown)
{
	if (kDown & (KEY_A | KEY_B)) { mode = MODE_DRAW; return; }
	if (kDown & KEY_DUP) viewSel = (viewSel + VIEW_ROWS - 1) % VIEW_ROWS;
	if (kDown & KEY_DDOWN) viewSel = (viewSel + 1) % VIEW_ROWS;
	int dir = (kDown & KEY_DRIGHT) ? 1 : (kDown & KEY_DLEFT) ? -1 : 0;
	if (!dir) return;
	static const int HUD[] = { 0, 8, 16, 32 }, ISO[] = { 0, 8, 16 }, AUTO[] = { 0, 2, 5 };
	int k;
	switch (viewSel)
	{
	case 0: flipPreview = !flipPreview; break;
	case 1: tilePreview = !tilePreview; break;
	case 2: for (k = 0; k < 4 && HUD[k] != hudGrid; k++); hudGrid = HUD[(k + 4 + dir) % 4]; break;
	case 3: for (k = 0; k < 3 && ISO[k] != isoGrid; k++); isoGrid = ISO[(k + 3 + dir) % 3]; break;
	case 4: for (k = 0; k < 3 && AUTO[k] != autosaveMin; k++); autosaveMin = AUTO[(k + 3 + dir) % 3]; break;
	}
}

static void update_more_menus(u32 kDown, u32 kHeld, touchPosition touch)
{
	switch (mode)
	{
	case MODE_LAYERS: update_layers(kDown); break;
	case MODE_LAYER_ACT:
		list_nav(kDown, &layerActSel, LAYER_ACT_COUNT);
		if (kDown & KEY_B) mode = MODE_LAYERS;
		if (kDown & KEY_A) { mode = MODE_LAYERS; run_layer_action(layerActSel); }
		break;
	case MODE_ANCHOR:
		if (kDown & KEY_DLEFT) anchorX = anchorX > 0 ? anchorX - 1 : 0;
		if (kDown & KEY_DRIGHT) anchorX = anchorX < 2 ? anchorX + 1 : 2;
		if (kDown & KEY_DUP) anchorY = anchorY > 0 ? anchorY - 1 : 0;
		if (kDown & KEY_DDOWN) anchorY = anchorY < 2 ? anchorY + 1 : 2;
		if (kDown & KEY_B) mode = MODE_MENU;
		if (kDown & KEY_A)
		{
			sel_clear(&doc);
			hist_begin(&doc);
			if (doc_resize(&doc, resizeW, resizeH, anchorX, anchorY)) { hist_commit(); doc.dirty = true; view_fit(); set_status("Canvas resized"); }
			else { hist_abort(); set_status("Couldn't resize"); }
			after_doc_change();
			mode = MODE_DRAW;
		}
		break;
	case MODE_IMPORT:
		list_nav(kDown, &importSel, importCount);
		if (kDown & KEY_B) mode = MODE_MENU;
		if ((kDown & KEY_A) && importCount) { importAsSel = 0; mode = MODE_IMPORT_AS; }
		break;
	case MODE_IMPORT_AS:
		list_nav(kDown, &importAsSel, 4);
		if (kDown & KEY_B) mode = MODE_IMPORT;
		if (kDown & KEY_A)
		{
			mode = MODE_DRAW;
			if (importAsSel == 0 && doc.dirty) confirm(ACT_IMPORT_PROJECT, "Discard unsaved changes?");
			else import_as(importAsSel);
		}
		break;
	case MODE_COLORS:
		list_nav(kDown, &colorsSel, 2);
		if (kDown & KEY_B) mode = MODE_MENU;
		if (kDown & KEY_A)
		{
			if (colorsSel == 0)
			{
				if (curSwatch < 0) { set_status("Tap the start swatch first, then Colors > Color ramp"); mode = MODE_DRAW; }
				else mode = MODE_RAMP;
			}
			else adjust_begin();
		}
		break;
	case MODE_RAMP:
		if (kDown & KEY_B) mode = MODE_DRAW;
		if ((kDown & KEY_TOUCH) && touch.py < SW_H * 2)
		{
			make_ramp((touch.py / SW_H) * SW_COLS + touch.px / SW_W);
			mode = MODE_DRAW;
			touchTarget = TOUCH_STRIP;    // don't draw with this tap
		}
		break;
	case MODE_ADJUST: update_adjust(kDown); break;
	case MODE_VIEW: update_view(kDown); break;
	default: break;
	}
	(void)kHeld;
}

// ---- draw mode input -------------------------------------------------------------

static void update_shift(u32 kDown, u32 kHeld, touchPosition touch)
{
	int ox = shiftX, oy = shiftY;
	if (kDown & KEY_DLEFT) shiftX--;
	if (kDown & KEY_DRIGHT) shiftX++;
	if (kDown & KEY_DUP) shiftY--;
	if (kDown & KEY_DDOWN) shiftY++;
	int cx, cy;
	screen_to_canvas(touch.px, touch.py, &cx, &cy);
	if ((kDown & KEY_TOUCH)) { shiftLastCX = cx; shiftLastCY = cy; }
	else if (kHeld & KEY_TOUCH)
	{
		shiftX += cx - shiftLastCX;
		shiftY += cy - shiftLastCY;
		shiftLastCX = cx;
		shiftLastCY = cy;
	}
	if (ox != shiftX || oy != shiftY)
	{
		shift_set(&doc, shiftX, shiftY);
		after_doc_change();
	}
	if (kDown & (KEY_A | KEY_B))
	{
		shift_end(&doc, kDown & KEY_A);
		after_doc_change();
		mode = MODE_DRAW;
	}
}

static void update_draw(u32 kDown, u32 kHeld, u32 kUp, touchPosition touch)
{
	if (kDown & KEY_START) { finish_floating(); mode = MODE_MENU; menuSel = 0; return; }
	if ((kDown & KEY_A) && touchTarget == TOUCH_NONE)
	{
		if (tool == TOOL_STAMP) refresh_stamps();
		optTool = tool;
		mode = MODE_OPTIONS;
		optSel = 0;
		return;
	}
	if (kDown & KEY_SELECT) previewMode = !previewMode;
	if (kDown & KEY_X) gridOn = !gridOn;
	if (touchTarget == TOUCH_NONE) // no tool changes mid-stroke
	{
		if (kDown & KEY_Y) { open_layers(); return; }
		if (kDown & (KEY_DUP | KEY_DDOWN))   // tool list is vertical: up/down picks the tool
		{
			listPos = (listPos + TOOL_COUNT + ((kDown & KEY_DDOWN) ? 1 : -1)) % TOOL_COUNT;
			if (listPos == TOOL_MIRROR)
			{
				// Mirror is a setting for all tools: show it, keep drawing with the current tool
				optTool = TOOL_MIRROR;
				optSel = 0;
				mode = MODE_OPTIONS;
				return;
			}
			set_tool(listPos);
		}
	}
	if (kDown & KEY_DRIGHT) view_zoom(+1);
	if (kDown & KEY_DLEFT) view_zoom(-1);

	if (touchTarget == TOUCH_NONE)
	{
		if (kDown & KEY_L)
		{
			int ow = doc.w, oh = doc.h;
			if (sel_floating()) sel_cancel(&doc);   // undo a move/paste in progress
			else if (hist_undo(&doc) && (doc.w != ow || doc.h != oh)) sel_clear(&doc);
			after_doc_change();
		}
		int rw = doc.w, rh = doc.h;
		if ((kDown & KEY_ZL) && !sel_floating() && hist_redo(&doc))
		{
			if (doc.w != rw || doc.h != rh) sel_clear(&doc);
			after_doc_change();
		}
		if (kDown & KEY_B)
		{
			if (tool == TOOL_SELECT && sel_active())
			{
				sel_clear(&doc);
				after_doc_change();
			}
			else if (tool == TOOL_ERASER)
				set_tool(eraserReturn == TOOL_ERASER ? TOOL_PENCIL : eraserReturn);
			else
			{
				// B toggles the eraser
				eraserReturn = tool;
				set_tool(TOOL_ERASER);
			}
		}
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
	uint8_t value = tool == TOOL_ERASER ? PIX_TRANSPARENT : strokeValue;

	if ((tool == TOOL_SELECT || tool == TOOL_STAMP) && !(kHeld & KEY_R) &&
	    (touchTarget == TOUCH_CANVAS || ((kDown & KEY_TOUCH) && touch.py >= STRIP_H)))
	{
		int cx, cy;
		screen_to_canvas(touch.px, touch.py, &cx, &cy);
		if (kDown & KEY_TOUCH)
		{
			touchTarget = TOUCH_CANVAS;
			startCX = cx;
			startCY = cy;
			if (tool == TOOL_STAMP)
			{
				free(stampPx);
				stampPx = NULL;
				if (!stampCount) { set_status("No stamps yet - save one with Select, A"); selDrag = SEL_IDLE; }
				else if (!layer_editable()) selDrag = SEL_IDLE;
				else if (stamp_load(STAMP_DIR, stampNames[stampSel], &doc, &stampPx, &stampW, &stampH)) { set_status("Couldn't load stamp"); selDrag = SEL_IDLE; }
				else
				{
					// a tap places it at original size, centered on the stylus
					moveOffX = stampW / 2;
					moveOffY = stampH / 2;
					sel_float_pixels(&doc, stampPx, stampW, stampH, cx - moveOffX, cy - moveOffY);
					sel_set_float_mirror(mirrorOn ? mirrorMode : MIRROR_OFF);
					selDrag = stampDragSize ? SEL_SIZING : SEL_MOVING;
				}
			}
			else if (sel_contains(cx, cy))
			{
				if (!sel_floating() && !layer_editable()) selDrag = SEL_IDLE;
				else
				{
					if (!sel_floating()) sel_lift(&doc);
					Rect r = sel_rect();
					moveOffX = cx - r.x;
					moveOffY = cy - r.y;
					selDrag = SEL_MOVING;
				}
			}
			else if (selMode == SELMODE_WAND)
			{
				sel_wand(&doc, cx, cy, wandAll);
				selDrag = SEL_IDLE;
			}
			else if (selMode == SELMODE_LASSO)
			{
				sel_clear(&doc);
				lassoCount = 0;
				lassoPts[0] = cx;
				lassoPts[1] = cy;
				lassoCount = 1;
				selDrag = SEL_LASSO;
			}
			else
			{
				sel_set(&doc, cx, cy, cx, cy);   // also drops any floating pixels
				selDrag = SEL_DRAWING;
			}
		}
		else if ((kHeld & KEY_TOUCH) && selDrag == SEL_DRAWING)
			sel_set(&doc, startCX, startCY, cx, cy);
		else if ((kHeld & KEY_TOUCH) && selDrag == SEL_LASSO && lassoCount < LASSO_MAX &&
		         (cx != lassoPts[(lassoCount - 1) * 2] || cy != lassoPts[(lassoCount - 1) * 2 + 1]))
		{
			lassoPts[lassoCount * 2] = cx;
			lassoPts[lassoCount * 2 + 1] = cy;
			lassoCount++;
		}
		else if ((kHeld & KEY_TOUCH) && selDrag == SEL_MOVING)
			sel_move_to(&doc, cx - moveOffX, cy - moveOffY);
		else if ((kHeld & KEY_TOUCH) && selDrag == SEL_SIZING && (cx != startCX || cy != startCY))
		{
			// stamp scaled into the box from the touch point to the stylus (ZR keeps proportions)
			int w = abs(cx - startCX) + 1, h = abs(cy - startCY) + 1;
			if (kHeld & KEY_ZR)
			{
				float s = fmaxf((float)w / stampW, (float)h / stampH);
				w = (int)lroundf(stampW * s);
				h = (int)lroundf(stampH * s);
				if (w < 1) w = 1;
				if (h < 1) h = 1;
			}
			if (w > DOC_MAX_W * 2) w = DOC_MAX_W * 2;
			if (h > DOC_MAX_H * 2) h = DOC_MAX_H * 2;
			uint8_t* scaled = malloc((size_t)w * h);
			if (scaled)
			{
				for (int j = 0; j < h; j++)
					for (int i = 0; i < w; i++)
						scaled[j * w + i] = stampPx[(j * stampH / h) * stampW + i * stampW / w];
				int x = cx >= startCX ? startCX : startCX - w + 1;
				int y = cy >= startCY ? startCY : startCY - h + 1;
				sel_float_replace(&doc, scaled, w, h, x, y);
				free(scaled);
			}
		}
		if (kHeld & KEY_TOUCH)
		{
			selLastCX = cx;
			selLastCY = cy;
		}

		if (!(kHeld & KEY_TOUCH))
		{
			if (selDrag == SEL_DRAWING && startCX == selLastCX && startCY == selLastCY)
				sel_clear(&doc);                 // a tap outside deselects
			if (selDrag == SEL_LASSO)
			{
				if (lassoCount > 2) sel_lasso(&doc, lassoPts, lassoCount);
				else sel_clear(&doc);
				lassoCount = 0;
			}
			if (tool == TOOL_STAMP && (selDrag == SEL_MOVING || selDrag == SEL_SIZING))
			{
				sel_commit(&doc);                // place the stamp
				sel_clear(&doc);
			}
			free(stampPx);
			stampPx = NULL;
			selDrag = SEL_IDLE;
			touchTarget = TOUCH_NONE;
		}
		after_doc_change();
		return;
	}

	if (kDown & KEY_TOUCH)
	{
		if (touch.py < STRIP_H)
		{
			touchTarget = TOUCH_STRIP;
			int slot = (touch.py / SW_H) * SW_COLS + touch.px / SW_W;
			if (touch.py >= SW_H * 2)
			{
				int r = touch.px / RECENT_W;     // recent colors row
				if (r < recentCount)
				{
					pick_color(recent[r]);
					if (tool == TOOL_ERASER || tool == TOOL_EYEDROPPER) set_tool(TOOL_PENCIL);
				}
			}
			else if (slot >= 0 && slot < DOC_PALETTE_SIZE)
			{
				if (kHeld & KEY_R)
				{
					// R + tap a swatch: save the current color into it
					doc.swatches[slot] = curColor;
					curSwatch = slot;
					doc.dirty = true;
					set_status("Saved color to swatch");
					return;
				}
				if (slot == curSwatch && curColor == doc.swatches[slot])
				{
					finish_floating();   // recoloring renumbers pixels; nothing may be floating
					picker_open(slot, curColor, doc.swatches);
					picker_set_linked(doc.linkedSwatches);
					mode = MODE_PICKER;
					touchTarget = TOUCH_NONE;
					return;
				}
				curSwatch = slot;
				curColor = doc.swatches[slot];
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
			else if (!layer_editable())
				touchTarget = TOUCH_STRIP;           // ignore this touch
			else
			{
				// find/add the color first: this may renumber pixels, so do it before snapshots
				strokeValue = doc_color_index(&doc, curColor, true);
				value = tool == TOOL_ERASER ? PIX_TRANSPARENT : strokeValue;
				if (tool == TOOL_SHADE)
				{
					build_shade_map();
					tools_set_value_map(shadeMap);
				}
				hist_begin(&doc);
				tools_begin(&doc, mirrorOn ? mirrorMode : MIRROR_OFF);
				tools_set_clip(sel_active() ? sel_contains : NULL);   // paint inside the selection only
				strokeActive = true;
				if (tool == TOOL_FILL && fillMode != FILL_GRADIENT)
					tools_fill(cx, cy, strokeValue, fillMode == FILL_REPLACE);
				else if (tool == TOOL_FILL)
					tools_gradient_fill(cx, cy, cx, cy, strokeValue);
				else if (tool == TOOL_PENCIL || tool == TOOL_ERASER || tool == TOOL_SHADE)
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
		else if (strokeActive && (tool == TOOL_PENCIL || tool == TOOL_ERASER || tool == TOOL_SHADE) && !(kDown & KEY_TOUCH))
			tools_stroke_to(cx, cy, value, &brushes[tool]);
		else if (strokeActive && tool == TOOL_FILL && fillMode == FILL_GRADIENT && !(kDown & KEY_TOUCH))
		{
			tools_restore();
			tools_gradient_fill(startCX, startCY, cx, cy, strokeValue);
		}
		else if (strokeActive && is_shape(tool))
		{
			if (kDown & KEY_B)
			{
				// cancel the shape
				tools_restore();
				hist_abort();
				tools_set_clip(NULL);
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
				if (tool != TOOL_ERASER && tool != TOOL_SHADE)
					push_recent(curColor);
			}
			else
				hist_abort();
			tools_set_value_map(NULL);
			tools_set_clip(NULL);
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

	// HUD layout grid
	if (hudGrid && hudGrid * z >= 4)
	{
		u32 hc = C2D_Color32(0x30, 0xD0, 0xFF, 0x90);
		float top = sy < VIEW_Y ? VIEW_Y : sy;
		for (int x = hudGrid; x < doc.w; x += hudGrid)
			C2D_DrawRectSolid(sx + x * z, top, 0.5f, 1, sy + doc.h * z - top, hc);
		for (int y = hudGrid; y < doc.h; y += hudGrid)
			if (sy + y * z >= VIEW_Y)
				C2D_DrawRectSolid(sx, sy + y * z, 0.5f, doc.w * z, 1, hc);
	}
	// isometric 2:1 grid: lines with slope +-1/2, clipped to the canvas
	if (isoGrid && isoGrid * z >= 4)
	{
		u32 ic = C2D_Color32(0x90, 0xFF, 0x60, 0x80);
		float W = doc.w, H = doc.h;
		for (int sgn = -1; sgn <= 1; sgn += 2)
			for (float c = -W; c <= H + W; c += isoGrid)
			{
				// y = sgn * x / 2 + c, for x in [0, W]
				float xa = 0, xb = W;
				float ya = c, yb = sgn * W / 2 + c;
				// clip y to [0, H] by moving x
				if (ya < 0) { xa = (0 - c) * 2 * sgn; ya = 0; }
				if (ya > H) { xa = (H - c) * 2 * sgn; ya = H; }
				if (yb < 0) { xb = (0 - c) * 2 * sgn; yb = 0; }
				if (yb > H) { xb = (H - c) * 2 * sgn; yb = H; }
				if (xa < 0 || xa > W || xb < 0 || xb > W || xb <= xa) continue;
				C2D_DrawLine(sx + xa * z, sy + ya * z, ic, sx + xb * z, sy + yb * z, ic, 1, 0.5f);
			}
	}

	// mirror guides
	if (mirrorOn && tool != TOOL_SELECT && tool != TOOL_EYEDROPPER)
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
		C2D_DrawRectSolid(x + 1, y + 1, 0.5f, SW_W - 2, SW_H - 2, rgba_to_c2d(doc.swatches[i]));
	}
	// recent colors row
	for (int i = 0; i < RECENT_COUNT; i++)
	{
		float x = i * RECENT_W, y = SW_H * 2;
		if (i < recentCount)
			C2D_DrawRectSolid(x + 1, y + 2, 0.5f, RECENT_W - 2, SW_H - 3, rgba_to_c2d(recent[i]));
		else
			C2D_DrawRectSolid(x + 1, y + 2, 0.5f, RECENT_W - 2, SW_H - 3, C2D_Color32(0x3A, 0x3D, 0x48, 255));
	}
	if (mode == MODE_RAMP)
		C2D_DrawRectSolid(0, 0, 0.5f, BOT_W, SW_H * 2, C2D_Color32(0xFF, 0xAA, 0x28, 0x40));
	if (curSwatch >= 0)
	{
		float hx = (curSwatch % SW_COLS) * SW_W, hy = (curSwatch / SW_COLS) * SW_H;
		gfx_rect_outline(hx, hy, SW_W, SW_H, 2, C2D_Color32(0, 0, 0, 255));
		gfx_rect_outline(hx + 1, hy + 1, SW_W - 2, SW_H - 2, 1, C2D_Color32(255, 255, 255, 255));
	}

	// lasso being drawn
	if (selDrag == SEL_LASSO && lassoCount > 1)
	{
		u32 lc = C2D_Color32(255, 255, 255, 255);
		for (int i = 1; i < lassoCount; i++)
			C2D_DrawLine(sx + (lassoPts[i * 2 - 2] + 0.5f) * z, sy + (lassoPts[i * 2 - 1] + 0.5f) * z, lc,
			             sx + (lassoPts[i * 2] + 0.5f) * z, sy + (lassoPts[i * 2 + 1] + 0.5f) * z, lc, 1, 0.5f);
	}

	// shaped selection: outline the selected pixels
	if (sel_active() && (sel_has_mask() || (sel_floating() && tool == TOOL_SELECT)))
	{
		Rect r = sel_rect();
		static int phase2;
		phase2 = (phase2 + 1) % 32;
		int edges = 0;
		for (int j = 0; j < r.h && edges < 3000; j++)
			for (int i = 0; i < r.w && edges < 3000; i++)
			{
				if (!sel_mask_at(i, j)) continue;
				float px = sx + (r.x + i) * z, py = sy + (r.y + j) * z;
				u32 c = ((r.x + i + r.y + j + phase2 / 8) & 1) ? C2D_Color32(255, 255, 255, 255) : C2D_Color32(0, 0, 0, 255);
				if (!sel_mask_at(i, j - 1) && py >= VIEW_Y) { C2D_DrawRectSolid(px, py, 0.5f, z < 1 ? 1 : z, 1, c); edges++; }
				if (!sel_mask_at(i, j + 1) && py + z >= VIEW_Y) { C2D_DrawRectSolid(px, py + z - 1, 0.5f, z < 1 ? 1 : z, 1, c); edges++; }
				if (!sel_mask_at(i - 1, j) && py >= VIEW_Y) { C2D_DrawRectSolid(px, py, 0.5f, 1, z < 1 ? 1 : z, c); edges++; }
				if (!sel_mask_at(i + 1, j) && py >= VIEW_Y) { C2D_DrawRectSolid(px + z - 1, py, 0.5f, 1, z < 1 ? 1 : z, c); edges++; }
			}
	}
	// rectangle selection marquee
	else if (sel_active())
	{
		Rect r = sel_rect();
		float x0 = sx + r.x * z - 1, y0 = sy + r.y * z - 1, w = r.w * z + 2, h = r.h * z + 2;
		static int phase;
		phase = (phase + 1) % 16;
		u32 cw = C2D_Color32(255, 255, 255, 255), cb = C2D_Color32(0, 0, 0, 255);
		for (float t = 0; t < w; t += 4)
		{
			u32 c = ((int)(t + phase / 4) / 4) & 1 ? cw : cb;
			float len = t + 4 > w ? w - t : 4;
			if (y0 >= VIEW_Y) C2D_DrawRectSolid(x0 + t, y0, 0.5f, len, 1, c);
			if (y0 + h - 1 >= VIEW_Y) C2D_DrawRectSolid(x0 + t, y0 + h - 1, 0.5f, len, 1, c);
		}
		for (float t = 0; t < h; t += 4)
		{
			u32 c = ((int)(t + phase / 4) / 4) & 1 ? cw : cb;
			float yy = y0 + t, len = t + 4 > h ? h - t : 4;
			if (yy + len <= VIEW_Y) continue;
			if (yy < VIEW_Y) { len -= VIEW_Y - yy; yy = VIEW_Y; }
			C2D_DrawRectSolid(x0, yy, 0.5f, 1, len, c);
			C2D_DrawRectSolid(x0 + w - 1, yy, 0.5f, 1, len, c);
		}
	}

	if (mode != MODE_DRAW && mode != MODE_SHIFT && mode != MODE_RAMP && mode != MODE_ADJUST && mode != MODE_LAYERS)
		C2D_DrawRectSolid(0, 0, 0.5f, BOT_W, BOT_H, C2D_Color32(0, 0, 0, 0x90));
}

// Draws the canvas into a box at the largest scale <= 1:1 that fits, centered.
static void draw_preview_box(float x, float y, float w, float h)
{
	float s = 1.0f;
	while (doc.w * s > w || doc.h * s > h)
		s /= 2;
	int t = tilePreview ? 3 : 1;
	while (doc.w * s * t > w || doc.h * s * t > h)
		s /= 2;
	float px = floorf(x + (w - doc.w * s * t) / 2), py = floorf(y + (h - doc.h * s * t) / 2);
	for (int j = 0; j < t; j++)
		for (int i = 0; i < t; i++)
			gfx_draw_canvas_ex(&doc, px + i * doc.w * s, py + j * doc.h * s, s, flipPreview);
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
	gfx_text(10, 24, 0.4f, C_DIM, "TOOLS  D-pad up/down   A: options");
	for (int i = 0; i < TOOL_COUNT; i++)
	{
		float y = 38 + i * 11;
		bool on = i == tool || (i == TOOL_MIRROR && mode == MODE_OPTIONS && optTool == TOOL_MIRROR);
		if (on)
			C2D_DrawRectSolid(8, y - 1, 0.5f, 180, 11, C_SEL);
		if (i == TOOL_MIRROR)
			C2D_DrawRectSolid(8, y - 2, 0.5f, 180, 1, C_PANEL);  // separator: Mirror is a setting
		gfx_text(14, y - 2, 0.4f, on || (i == TOOL_MIRROR && mirrorOn) ? C_ACCENT : C_TEXT, "%s", TOOL_NAMES[i]);
		char sum[32];
		tool_summary(i, sum, sizeof(sum));
		if (sum[0])
			gfx_text(84, y - 2, 0.36f, C_DIM, "%s", sum);
	}

	// color
	float cy = 38 + TOOL_COUNT * 11 + 3;
	C2D_DrawRectSolid(10, cy, 0.5f, 18, 18, C_TEXT);
	C2D_DrawRectSolid(11, cy + 1, 0.5f, 16, 16, rgba_to_c2d(curColor));
	gfx_text(34, cy + 2, 0.46f, C_TEXT, "#%06lX", (unsigned long)(curColor >> 8));
	if (curSwatch >= 0)
		gfx_text(104, cy + 3, 0.4f, C_DIM, "swatch %d", curSwatch + 1);
	else
		gfx_text(104, cy + 3, 0.4f, C_DIM, "R+tap swatch: save");

	// info
	float iy = cy + 21;
	const Layer* CL = &doc.layers[doc.curLayer];
	gfx_text(10, iy, 0.4f, C_DIM, "Canvas %dx%d  Zoom %s  Grid %s", doc.w, doc.h, zoom_label(), gridOn ? "on" : "off");
	gfx_text(10, iy + 11, 0.4f, CL->locked || !CL->visible ? C_ACCENT : C_DIM, "Layer: %s%s%s  %d%%  (Y)", CL->name,
	         CL->locked ? " [locked]" : "", CL->visible ? "" : " [hidden]", CL->opacity * 100 / 255);

	// true-size preview
	C2D_DrawRectSolid(196, 28, 0.5f, 198, 180, C_BG_BOT);
	draw_preview_box(197, 29, 196, 178);

	// footer
	if (statusTimer > 0)
		gfx_text(10, 220, 0.5f, C_ACCENT, "%s", status);
	else if (mode == MODE_PICKER)
		gfx_text(10, 220, 0.45f, C_DIM, "Pick a color below.  A: OK   B: cancel");
	else if (tool == TOOL_SELECT)
		gfx_text(10, 222, 0.42f, C_DIM, "Drag: select / move.  A: actions  B: deselect");
	else if (tool == TOOL_STAMP)
		gfx_text(10, 222, 0.42f, C_DIM, stampDragSize ? "Drag a box to size it (ZR: keep shape).  A: options"
		                                               : "Touch to place, drag to position.  A: options");
	else if (is_shape(tool))
		gfx_text(10, 222, 0.42f, C_DIM, "Drag to draw. Hold ZR to snap, B cancels.");
	else
		gfx_text(10, 222, 0.42f, C_DIM, "L undo ZL redo R eyedrop B eraser X grid <> zoom");
}

static void draw_top_preview(void)
{
	if (tilePreview)
	{
		draw_preview_box(0, 0, 400, 222);
		gfx_text(8, 224, 0.42f, C_DIM, "Tile preview 3x3%s   SELECT: back to tools", flipPreview ? ", flipped" : "");
		return;
	}
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
		gfx_draw_canvas_ex(&doc, x, y, s, flipPreview);
		gfx_text(x, y + doc.h * s + 2, 0.4f, C_DIM, "%dx", s);
		x += doc.w * s + gap;
	}
	gfx_text(8, 224, 0.42f, C_DIM, "Preview%s   SELECT: back to tools", flipPreview ? " (flipped)" : "");
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
	{
		const char* items[MENU_COUNT];
		memcpy(items, MENU_ITEMS, sizeof(items));
		items[MENU_LINKED] = doc.linkedSwatches ? "Linked swatches: On" : "Linked swatches: Off";
		draw_list("Menu", items, MENU_COUNT, menuSel, "");
	}
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
		OptKind rows[OPT_MAX_ROWS];
		int n = opt_rows(optTool, rows);
		// show at most 9 rows; scroll to keep the cursor visible
		const int maxVis = 9;
		int vis = n < maxVis ? n : maxVis;
		int first = optSel - vis / 2;
		if (first > n - vis) first = n - vis;
		if (first < 0) first = 0;
		float rh = 20, ph = 34 + vis * rh + 22, top = floorf((240 - ph) / 2);
		C2D_DrawRectSolid(40, top, 0.5f, 320, ph, C_PANEL);
		gfx_rect_outline(40, top, 320, ph, 1, C_ACCENT);
		gfx_text(52, top + 6, 0.55f, C_TEXT, "%s %s", TOOL_NAMES[optTool], optTool == TOOL_SELECT ? "actions" : "options");
		if (n > vis)
			gfx_text(300, top + 8, 0.42f, C_DIM, "%d/%d", optSel + 1, n);
		bool anyAction = false;
		for (int k = 0; k < n; k++) anyAction |= rows[k] >= OPT_ACT_FIRST;
		for (int r = 0; r < vis; r++)
		{
			int i2 = first + r;
			float y = top + 30 + r * rh;
			bool act = rows[i2] >= OPT_ACT_FIRST;
			if (i2 == optSel)
				C2D_DrawRectSolid(46, y - 3, 0.5f, 308, rh - 1, C_SEL);
			gfx_text(56, y, 0.5f, i2 == optSel ? C_ACCENT : C_TEXT, "%s", opt_label(rows[i2]));
			if (!act)
			{
				char val[64];
				opt_value(rows[i2], val, sizeof(val));
				gfx_text(176, y, 0.5f, C_TEXT, "< %s >", val);
			}
		}
		if (first > 0)
			gfx_text(340, top + 30, 0.45f, C_DIM, "\xE2\x96\xB2");
		if (first + vis < n)
			gfx_text(340, top + 30 + (vis - 1) * rh, 0.45f, C_DIM, "\xE2\x96\xBC");
		gfx_text(52, top + 30 + vis * rh + 4, 0.42f, C_DIM, anyAction ? "Up/Down: choose   A: do it / close   B: close"
		                                                                : "Up/Down: option   Left/Right: change   A/B: close");
	}
	else if (mode == MODE_EXPORT)
	{
		C2D_DrawRectSolid(30, 8, 0.5f, 340, 224, C_PANEL);
		gfx_rect_outline(30, 8, 340, 224, 1, C_DIM);
		gfx_text(42, 14, 0.55f, C_TEXT, "Export PNG  (frame %d, %dx%d)", doc.curFrame + 1, doc.w, doc.h);
		const int visible = 10;
		int first = exportSel - visible + 2 < 0 ? 0 : exportSel - visible + 2;
		if (first > EXPORT_ROWS - visible) first = EXPORT_ROWS - visible;
		for (int r = first; r < first + visible; r++)
		{
			float y = 36 + (r - first) * 17;
			if (r == exportSel)
				C2D_DrawRectSolid(36, y - 2, 0.5f, 328, 16, C_SEL);
			u32 c = r == exportSel ? C_ACCENT : C_TEXT;
			if (r == 0)
				gfx_text(46, y, 0.48f, c, "%s Recommended  (1x 2x 4x 8x)", export_is_recommended() ? "[x]" : "[  ]");
			else if (r == EXPORT_ROWS - 1)
				gfx_text(46, y, 0.48f, c, ">> Export now");
			else
			{
				int i = r - 1, w, h;
				bool even, ok = export_dims(i, &w, &h, &even);
				char label[16];
				if (EXPORT_SIZES[i].mult) snprintf(label, sizeof(label), "%dx", EXPORT_SIZES[i].mult);
				else snprintf(label, sizeof(label), "%d px", EXPORT_SIZES[i].fixed);
				gfx_text(46, y, 0.48f, c, "%s %-7s %dx%d%s", exportOn[i] ? "[x]" : "[  ]", label, w, h,
				         !ok ? "  too big" : !even ? "  (uneven pixels)" : "");
			}
		}
		gfx_text(42, 214, 0.42f, C_DIM, "Up/Down: choose   A: tick / run   B: back");
	}
	else if (mode == MODE_SHIFT)
	{
		C2D_DrawRectSolid(60, 70, 0.5f, 280, 100, C_PANEL);
		gfx_rect_outline(60, 70, 280, 100, 1, C_ACCENT);
		gfx_text(72, 80, 0.55f, C_TEXT, "Shift canvas (wraps around)");
		gfx_text(72, 104, 0.48f, C_TEXT, "Drag the canvas or use the D-pad.");
		gfx_text(72, 122, 0.48f, C_DIM, "Offset: %d, %d", shiftX, shiftY);
		gfx_text(72, 146, 0.45f, C_DIM, "A: keep    B: cancel");
	}
	else if (mode == MODE_LAYERS || mode == MODE_LAYER_ACT)
	{
		C2D_DrawRectSolid(16, 4, 0.5f, 368, 232, C_PANEL);
		gfx_rect_outline(16, 4, 368, 232, 1, C_ACCENT);
		gfx_text(26, 8, 0.52f, C_TEXT, "Layers");
		gfx_text(86, 10, 0.38f, C_DIM, "A actions  X show/hide  </> opacity  L/R move  B close");
		int rows[DOC_MAX_LAYERS];
		int n = layer_rows(rows);
		for (int k = 0; k < n; k++)
		{
			int i = rows[k];
			const Layer* L = &doc.layers[i];
			float y = 28 + k * 13;
			if (i == layerCursor)
				C2D_DrawRectSolid(20, y - 1, 0.5f, 360, 13, C_SEL);
			float x = L->folder >= 0 ? 44 : 26;
			u32 c = i == doc.curLayer ? C_ACCENT : (doc_layer_visible(&doc, i) ? C_TEXT : C_DIM);
			if (L->isFolder)
				gfx_text(x, y - 1, 0.44f, c, "%s %s (%d)", L->collapsed ? "[+]" : "[-]", L->name, doc_folder_size(&doc, i));
			else
				gfx_text(x, y - 1, 0.44f, c, "%s%s", i == doc.curLayer ? "> " : "  ", L->name);
			gfx_text(250, y - 1, 0.4f, C_DIM, "%s %s %3d%%", L->visible ? "shown " : "HIDDEN", L->locked ? "lock" : "    ",
			         L->opacity * 100 / 255);
		}
		if (mode == MODE_LAYER_ACT)
		{
			C2D_DrawRectSolid(150, 24, 0.5f, 224, 208, C2D_Color32(0x24, 0x27, 0x30, 255));
			gfx_rect_outline(150, 24, 224, 208, 1, C_DIM);
			for (int a = 0; a < LAYER_ACT_COUNT; a++)
			{
				float y = 30 + a * 16.5f;
				if (a == layerActSel)
					C2D_DrawRectSolid(154, y - 2, 0.5f, 216, 16, C_SEL);
				gfx_text(162, y, 0.46f, a == layerActSel ? C_ACCENT : C_TEXT, "%s", LAYER_ACTS[a]);
			}
		}
	}
	else if (mode == MODE_ANCHOR)
	{
		C2D_DrawRectSolid(70, 40, 0.5f, 260, 160, C_PANEL);
		gfx_rect_outline(70, 40, 260, 160, 1, C_ACCENT);
		gfx_text(82, 48, 0.52f, C_TEXT, "Canvas %dx%d -> %dx%d", doc.w, doc.h, resizeW, resizeH);
		gfx_text(82, 66, 0.42f, C_DIM, "Anchor: where the current art stays");
		for (int j = 0; j < 3; j++)
			for (int i = 0; i < 3; i++)
			{
				float x = 160 + i * 28, y = 88 + j * 28;
				C2D_DrawRectSolid(x, y, 0.5f, 24, 24, i == anchorX && j == anchorY ? C_ACCENT : C_SEL);
			}
		gfx_text(82, 180, 0.42f, C_DIM, "D-pad: anchor   A: resize   B: cancel");
	}
	else if (mode == MODE_IMPORT)
	{
		static const char* labels[64];
		for (int i = 0; i < importCount; i++) labels[i] = importFiles[i];
		draw_list("Import PNG", labels, importCount, importSel, "Put PNGs in /3ds/pixel-ui-designer/import");
	}
	else if (mode == MODE_IMPORT_AS)
		draw_list(importFiles[importSel], IMPORT_AS, 4, importAsSel, "");
	else if (mode == MODE_COLORS)
		draw_list("Colors", COLORS_ITEMS, 2, colorsSel, "");
	else if (mode == MODE_RAMP)
	{
		C2D_DrawRectSolid(40, 90, 0.5f, 320, 60, C_PANEL);
		gfx_rect_outline(40, 90, 320, 60, 1, C_ACCENT);
		gfx_text(52, 98, 0.5f, C_TEXT, "Color ramp from swatch %d", curSwatch + 1);
		gfx_text(52, 120, 0.44f, C_DIM, "Tap the end swatch on the bottom screen.  B: cancel");
	}
	else if (mode == MODE_ADJUST)
	{
		C2D_DrawRectSolid(60, 50, 0.5f, 280, 140, C_PANEL);
		gfx_rect_outline(60, 50, 280, 140, 1, C_ACCENT);
		gfx_text(72, 56, 0.52f, C_TEXT, "Adjust colors (%s)", sel_active() ? "selection" : "current layer");
		const char* lab[4] = { "Hue", "Saturation", "Brightness", "Invert" };
		for (int r = 0; r < 4; r++)
		{
			float y = 82 + r * 22;
			if (r == adjSel) C2D_DrawRectSolid(66, y - 3, 0.5f, 268, 20, C_SEL);
			gfx_text(76, y, 0.5f, r == adjSel ? C_ACCENT : C_TEXT, "%s", lab[r]);
			if (r == 0) gfx_text(196, y, 0.5f, C_TEXT, "< %+d\xC2\xB0 >", adjHue);
			else if (r == 1) gfx_text(196, y, 0.5f, C_TEXT, "< %+d%% >", adjSat);
			else if (r == 2) gfx_text(196, y, 0.5f, C_TEXT, "< %+d%% >", adjBri);
			else gfx_text(196, y, 0.5f, C_TEXT, "< %s >", adjInvert ? "On" : "Off");
		}
		gfx_text(72, 172, 0.42f, C_DIM, "Up/Down: row  Left/Right: change  A: apply  B: cancel");
	}
	else if (mode == MODE_VIEW)
	{
		C2D_DrawRectSolid(50, 50, 0.5f, 300, 150, C_PANEL);
		gfx_rect_outline(50, 50, 300, 150, 1, C_ACCENT);
		gfx_text(62, 56, 0.52f, C_TEXT, "View");
		const char* lab[VIEW_ROWS] = { "Flip preview", "Tile preview", "HUD grid", "Iso grid", "Autosave" };
		for (int r = 0; r < VIEW_ROWS; r++)
		{
			float y = 80 + r * 22;
			if (r == viewSel) C2D_DrawRectSolid(56, y - 3, 0.5f, 288, 20, C_SEL);
			gfx_text(66, y, 0.5f, r == viewSel ? C_ACCENT : C_TEXT, "%s", lab[r]);
			char v[32];
			view_value(r, v, sizeof(v));
			gfx_text(186, y, 0.5f, C_TEXT, "< %s >", v);
		}
		gfx_text(62, 186, 0.42f, C_DIM, "Up/Down: row   Left/Right: change   A/B: close");
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
	export_recommended();
	curColor = doc.swatches[0];
	if (file_exists(AUTOSAVE_PATH))
		set_status("Unsaved work was recovered: START > Open > _autosave");

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
				if (r == PICK_LIST)
				{
					uint32_t cols[DOC_PALETTE_SIZE];
					int n = picker_list(cols);
					if (doc.linkedSwatches)
					{
						hist_begin(&doc);
						if (doc_recolor(&doc, doc.swatches, cols, n)) { hist_commit(); after_doc_change(); }
						else hist_abort();
					}
					memcpy(doc.swatches, cols, n * sizeof(uint32_t));
					if (curSwatch >= 0 && curSwatch < n)
						curColor = doc.swatches[curSwatch];
					doc.dirty = true;
					char msg[64];
					snprintf(msg, sizeof(msg), "Set %d swatch%s", n, n == 1 ? "" : "es");
					set_status(msg);
				}
				else if (r == PICK_OK)
				{
					// the swatch and the current color change; the drawing does not
					curColor = picker_color();
					if (curSwatch >= 0 && doc.swatches[curSwatch] != curColor)
					{
						if (doc.linkedSwatches)
						{
							hist_begin(&doc);
							if (doc_recolor(&doc, &doc.swatches[curSwatch], &curColor, 1)) { hist_commit(); after_doc_change(); }
							else hist_abort();
						}
						doc.swatches[curSwatch] = curColor;
						doc.dirty = true;
					}
				}
				mode = MODE_DRAW;
				touchTarget = (kHeld & KEY_TOUCH) ? TOUCH_STRIP : TOUCH_NONE; // don't draw with the closing tap
			}
		}
		else if (mode == MODE_OPTIONS)
			update_options(kDown);
		else if (mode == MODE_SHIFT)
			update_shift(kDown, kHeld, touch);
		else if (mode >= MODE_LAYERS)
			update_more_menus(kDown, kHeld, touch);
		else
			update_menus(kDown);

		if (statusTimer > 0)
			statusTimer--;

		// autosave: a recovery copy, never the project file itself
		if (autosaveMin && doc.dirty && mode == MODE_DRAW && touchTarget == TOUCH_NONE && !sel_floating())
		{
			if (++autosaveFrames >= (unsigned)autosaveMin * 60 * 60)
			{
				autosaveFrames = 0;
				ensure_dirs();
				char keep[DOC_NAME_MAX];
				snprintf(keep, sizeof(keep), "%s", doc.name);
				if (!pxd_save(&doc, AUTOSAVE_PATH)) set_status("Autosaved recovery copy");
				snprintf(doc.name, sizeof(doc.name), "%s", keep);
			}
		}
		else if (!doc.dirty)
			autosaveFrames = 0;
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
