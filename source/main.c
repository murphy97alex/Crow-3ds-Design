// Pixel Crow - 3DS pixel painter
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
#include "anim.h"
#include "export_png.h"
#include "import.h"
#include "input.h"
#include "palette_io.h"
#include "pxd.h"
#include "settings.h"
#include "select.h"
#include "stamps.h"
#include "text.h"
#include "tools.h"

#define APP_NAME "Pixel Crow"
#ifndef APP_VERSION
#define APP_VERSION "dev"   // set by the Makefile from APP_VERSION_*
#endif
#define SAVE_ROOT "sdmc:/3ds/pixel-crow"
#define OLD_SAVE_ROOT "sdmc:/3ds/pixel-ui-designer"   // name before the rename
#define SAVE_DIR SAVE_ROOT "/projects"
#define STAMP_DIR SAVE_ROOT "/stamps"
#define EXPORT_DIR SAVE_ROOT "/export"
#define IMPORT_DIR SAVE_ROOT "/import"
#define AUTOSAVE_PATH SAVE_DIR "/_autosave.pxd"
#define SETTINGS_PATH SAVE_ROOT "/settings.txt"

// input.c mirrors libctru's button bits so it can be tested on a PC
#define SAME_KEY(name) ((u32)IN_##name == (u32)KEY_##name)
_Static_assert(SAME_KEY(A) && SAME_KEY(B) && SAME_KEY(SELECT) && SAME_KEY(START) && SAME_KEY(DRIGHT) &&
               SAME_KEY(DLEFT) && SAME_KEY(DUP) && SAME_KEY(DDOWN) && SAME_KEY(R) && SAME_KEY(L) && SAME_KEY(X) &&
               SAME_KEY(Y) && SAME_KEY(ZL) && SAME_KEY(ZR) && SAME_KEY(TOUCH), "input.h must match libctru's KEY_ bits");

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
#define VIEW_H view_h()   // canvas area: below the palette strip, above the frame bar (if shown)
static int view_h(void);

#define COL(r, g, b) C2D_Color32(r, g, b, 255)
#define C_BG_TOP   COL(0x20, 0x22, 0x28)
#define C_BG_BOT   COL(0x30, 0x32, 0x3A)
#define C_PANEL    COL(0x2C, 0x2F, 0x38)
#define C_TEXT     COL(0xF0, 0xF0, 0xF0)
#define C_DIM      COL(0xA0, 0xA4, 0xB0)
#define C_ACCENT   COL(0xFF, 0xAA, 0x28)
#define C_SEL      COL(0x44, 0x4A, 0x5C)

// ---- tools -----------------------------------------------------------------

typedef enum { TOOL_PENCIL, TOOL_ERASER, TOOL_FILL, TOOL_SHADE, TOOL_LINE, TOOL_RECT, TOOL_ELLIPSE, TOOL_SELECT, TOOL_STAMP, TOOL_TEXT, TOOL_EYEDROPPER, TOOL_MIRROR, TOOL_COUNT } Tool;
static const char* TOOL_NAMES[TOOL_COUNT] = { "Pencil", "Eraser", "Fill", "Shade", "Line", "Rectangle", "Ellipse", "Select", "Stamp", "Text", "Eyedropper", "Mirror" };

static bool eraserLine;
static bool is_shape(Tool t) { return t == TOOL_LINE || t == TOOL_RECT || t == TOOL_ELLIPSE || (t == TOOL_ERASER && eraserLine); }

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
static bool shadeBrightness;           // Shade: false = step to the nearest lighter/darker swatch
static bool lineCurve;                 // Line: Curve mode (drag the line, then drag to bend it)
static int curveStage;                 // 2 = line placed, waiting to be bent
static bool curveBend;
static int curveEndX, curveEndY, shapeEndX, shapeEndY;
enum { SELMODE_RECT, SELMODE_WAND, SELMODE_LASSO, SELMODE_COUNT };
static int selMode;
static bool wandAll;                   // magic wand: all pixels of the color, not just connected
static const char* SELMODE_NAMES[SELMODE_COUNT] = { "Rectangle", "Magic wand", "Lasso" };
static Mirror mirrorMode = MIRROR_X;   // axis for the Mirror tool
static bool mirrorOn = false;          // applies to every drawing tool
static int mirrorAX2, mirrorAY2;       // axis position x2 (between pixels allowed); 0 = centre
// eraserLine (declared above): Eraser draws straight lines instead of freehand
static bool tfCrisp = false, textCrisp = true;   // transform snaps to whole sizes
static SelOp selOp = SELOP_NEW;
static int wandTol = 0;                // magic wand tolerance %
static bool wandSampleAll = true;      // wand compares the colors you see
// Text tool
static int textFont = 4, textScale = 1, textSpacing = 0, textOutline = -1;  // outline: swatch index or -1
static char textStr[TEXT_MAX];
static bool textOn;                    // floating text waiting to be placed
#define TEXT_MAX_SCALE 32
// transform handles
enum { TF_NONE, TF_MOVE, TF_CORNER, TF_EDGE, TF_ROTATE };
static int tfDrag, tfHandle;
static float tfFixX, tfFixY;           // the point that stays put while resizing (canvas coords)
static float tfMoveOffX, tfMoveOffY;
static bool tfMoved;

static const char* SHAPE_NAMES[BRUSH_SHAPE_COUNT] = { "Square", "Round", "Dither" };
static const char* MIRROR_NAMES[MIRROR_COUNT] = { "Off", "Left-right", "Top-bottom", "Both" };

// options panel rows
typedef enum {
	OPT_SIZE, OPT_SHAPE, OPT_PIXPERF, OPT_STYLE, OPT_FILLMODE, OPT_MIRROR, OPT_MIRRORON, OPT_STAMP, OPT_CRISP,
	OPT_SELOP, OPT_TOLERANCE, OPT_SAMPLE, OPT_ERASEMODE, OPT_AXISPOS, OPT_SHADEMODE, OPT_LINEMODE,
	OPT_SHADEDIR, OPT_SELMODE, OPT_WANDALL, OPT_FONT, OPT_TSCALE, OPT_TSPACING, OPT_TOUTLINE,
	// actions (A runs them)
	OPT_ACT_FIRST, OPT_SELALL = OPT_ACT_FIRST, OPT_DESELECT, OPT_COPY, OPT_CUT, OPT_PASTE, OPT_DELETE,
	OPT_FLIPH, OPT_FLIPV, OPT_ROTATE, OPT_OUTLINE, OPT_SAVESTAMP, OPT_CROP, OPT_TEXTEDIT, OPT_TEXTPLACE,
	OPT_INVERT, OPT_TRANSFORM, OPT_APPLY, OPT_AXISRESET,
} OptKind;
#define OPT_MAX_ROWS 24

// stamps
static char stampNames[STAMP_MAX][DOC_NAME_MAX];
static int stampCount, stampSel;
static int optSel;
static Tool optTool;   // tool whose options panel is open
static int listPos;    // D-pad cursor in the tool list (Mirror is a settings entry, not a tool)

// ---- app state ---------------------------------------------------------------

typedef enum {
	MODE_DRAW, MODE_MENU, MODE_NEW, MODE_OPEN, MODE_CONFIRM, MODE_PICKER, MODE_OPTIONS, MODE_EXPORT, MODE_SHIFT,
	MODE_LAYERS, MODE_LAYER_ACT, MODE_ANCHOR, MODE_IMPORT, MODE_IMPORT_AS, MODE_COLORS, MODE_RAMP, MODE_ADJUST, MODE_VIEW,
	MODE_HOME, MODE_FRAMES, MODE_FRAME_ACT,
} Mode;
typedef enum { ACT_NONE, ACT_NEW, ACT_OPEN, ACT_EXIT, ACT_OVERWRITE, ACT_IMPORT_PROJECT, ACT_DELETE_GAL } Action;

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
enum { TOUCH_NONE, TOUCH_CANVAS, TOUCH_STRIP, TOUCH_PAN };
static int panLastX, panLastY;
static int touchTarget = TOUCH_NONE;
static int startCX, startCY;   // where the current stroke/shape began
static bool strokeActive;
enum { SEL_IDLE, SEL_DRAWING, SEL_PENDING, SEL_LASSO };
#define LASSO_MAX 1024
static int lassoPts[LASSO_MAX * 2], lassoCount;
static uint8_t shadeMap[256];
static uint32_t recent[RECENT_COUNT];
static int recentCount;
static int selLastCX, selLastCY;  // last stylus position (the release frame reads 0,0)
static int selDrag = SEL_IDLE;
static int shiftX, shiftY, shiftLastCX, shiftLastCY;

// menus
static int menuSel, newSel, openSel, openCount;
static char openFiles[128][DOC_NAME_MAX + 8];
static Action confirmAction;
static Mode confirmBack = MODE_MENU;   // where B in a confirm box goes
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
static int importHow, importCols = 4, importRows = 1;   // what a confirmed "new project" import does
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
// animation
static bool onionOn = true;        // onion skin (shown when there are 2+ frames)
static int onionCount = 1;         // frames before and after
static int onionPct = 35;          // opacity of the nearest frames
static bool animDirty = true;      // onion skin / playback need refreshing
static bool playOn;                // top screen plays the animation
static int playTag = -1;           // -1 = all frames
static int playSeq[ANIM_MAX_SEQ], playN, playPos, playShown = -1;
static u64 playNext;
static int frameActSel;
static bool frameThumbOk[DOC_MAX_FRAMES];
#define FRAME_THUMB_SLOT 16        // frame thumbnails use atlas slots 16..79 (layers use 0..15)
#define BAR_H 20                   // frame bar at the bottom of the touch screen (2+ frames)
#define BAR_Y (BOT_H - BAR_H)
#define PIXEL_BUDGET (24u << 20)   // memory for all frames x layers
static bool barHidden;             // View: hide the frame bar (frames stay reachable with X)
static bool bigPreview;            // Frames panel: the preview fills the top screen
static bool wrapDraw;              // View: tile drawing, strokes wrap around the canvas edges
static int sheetPad;               // export: transparent pixels between sprite sheet cells
static bool sheetTrim;             // export: cut the empty border off sprite sheet cells
// buttons
static InputMap inputMap;          // left-handed mode, Old 3DS stand-ins for ZL / ZR
static bool isNew3DS = true;
static int strokeLastCX, strokeLastCY;

static void scan_imports(void);
static void open_frames(void);
static bool bar_shown(void);
static void bar_touch(int x);
static void update_frames(u32 kDown);
static void run_frame_action(int a);
enum { FA_NEW, FA_DUP, FA_DEL, FA_LINK, FA_UNLINK, FA_COPYNEXT, FA_CLEAR, FA_MS, FA_FPS, FA_TAGNEW, FA_TAGSTART, FA_TAGEND, FA_TAGDIR,
       FA_TAGNAME, FA_TAGDEL, FA_PLAYTAG, FA_BIGVIEW, FA_COUNT };

static void import_as(int how);
static void open_home(bool fromCanvas);
static void gallery_delete(void);

static void set_status(const char* msg)
{
	snprintf(status, sizeof(status), "%s", msg);
	statusTimer = 180;
}

// Is there room for `extra` more bytes of pixel buffers? Explains if not.
static bool room_for(size_t extra)
{
	if (doc_pixel_bytes(&doc) + extra <= PIXEL_BUDGET) return true;
	set_status("Out of room: fewer layers/frames or a smaller canvas");
	return false;
}

static int view_h(void)
{
	return BOT_H - VIEW_Y - (doc.frameCount > 1 && !barHidden ? BAR_H : 0);
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
	if (stampCount == STAMP_MAX) set_status("Showing the first 64 stamps (delete some in /3ds/pixel-crow/stamps)");
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

static void curve_finish(void);

// Settle every edit that is still "in the air" (a curve waiting to be bent, floating pixels,
// text being placed). Anything that changes frame, layer, document or screen calls this first,
// so a pending edit can never land on a different cel than the one it was started on.
static void finish_pending(void)
{
	curve_finish();
	finish_floating();
}

// ---- text tool ---------------------------------------------------------------------

static void push_recent(uint32_t c);

static bool ask_multiline(const char* hint, char* buf, size_t n)
{
	SwkbdState kb;
	static char out[TEXT_MAX];
	memset(out, 0, sizeof(out));
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, n - 1);
	swkbdSetFeatures(&kb, SWKBD_MULTILINE);
	swkbdSetHintText(&kb, hint);
	swkbdSetInitialText(&kb, buf);
	if (swkbdInputText(&kb, out, sizeof(out)) != SWKBD_BUTTON_CONFIRM)
		return false;
	snprintf(buf, n, "%s", out);
	return buf[0] != 0;
}

// Text renders at 1x; its size comes from the transform (handles or the Size option).
static uint8_t* render_text(bool allowCompact, int* w, int* h)
{
	uint8_t fg = doc_color_index(&doc, curColor, allowCompact);
	uint8_t ol = textOutline >= 0 ? doc_color_index(&doc, doc.swatches[textOutline], false) : 0;
	return text_render(&FONTS[textFont], textStr, 1, textSpacing, fg, ol, w, h);
}

// Re-render floating text after its options, color or wording changed (keeps size/rotation).
static void text_refresh(void)
{
	if (!textOn || !sel_floating()) return;
	int w, h;
	uint8_t* px = render_text(false, &w, &h);
	if (!px) return;
	sel_set_source(&doc, px, w, h);
	free(px);
	after_doc_change();
}

static void text_place(void)
{
	if (!textOn) return;
	if (sel_floating())
	{
		sel_commit(&doc);
		sel_clear(&doc);
		push_recent(curColor);
	}
	textOn = false;
	after_doc_change();
}

static void text_start(int x, int y)
{
	if (!ask_multiline("Type text (new lines are OK)", textStr, sizeof(textStr)))
		return;
	int w, h;
	uint8_t* px = render_text(true, &w, &h);   // nothing floats yet, compaction is safe
	if (!px) return;
	sel_float_pixels(&doc, px, w, h, x, y);
	sel_set_float_mirror(mirrorOn ? mirrorMode : MIRROR_OFF);
	sel_set_mirror_axis(mirrorAX2, mirrorAY2);
	free(px);
	if (textScale > 1)
		sel_transform(&doc, x, y, w * textScale, h * textScale, 0, false);
	textOn = true;
	set_status("Drag to move, handles to size/rotate, tap to edit, B to place");
	after_doc_change();
}

static void text_edit(void)
{
	if (textOn && ask_multiline("Edit text", textStr, sizeof(textStr)))
		text_refresh();
}

// Set the floating text's size to a whole multiple (Size option), keeping its top-left.
static void text_apply_scale(void)
{
	if (!textOn || !sel_floating()) return;
	float x, y, w, h, a;
	int sw, sh;
	sel_transform_get(&x, &y, &w, &h, &a);
	sel_source_size(&sw, &sh);
	sel_transform(&doc, x, y, sw * textScale, sh * textScale, a, a != 0);
	after_doc_change();
}

static float canvas_sx(void);
static float canvas_sy(void);

// ---- curve line (two steps: drag the line, then drag to bend it) ----

static void curve_cancel(void)
{
	if (!curveStage) return;
	tools_restore();
	hist_abort();
	tools_set_clip(NULL);
	curveStage = 0;
	curveBend = false;
	after_doc_change();
}

// Keep the line as it is now (e.g. when switching tools before bending it).
static void curve_finish(void)
{
	if (!curveStage) return;
	if (tools_changed()) { hist_commit(); doc.dirty = true; push_recent(curColor); }
	else hist_abort();
	tools_set_clip(NULL);
	curveStage = 0;
	curveBend = false;
	after_doc_change();
}

// ---- transform handles -----------------------------------------------------------------
// Handles 0-3: corners TL TR BR BL (resize, proportional unless ZR). 4-7: edges T R B L
// (stretch one way). 8: rotate (dot above the top edge). Positions are unrotated offsets from
// the box centre in half-sizes.
static const float HX[9] = { -1, 1, 1, -1, 0, 1, 0, -1, 0 };
static const float HY[9] = { -1, -1, 1, 1, -1, 0, 1, 0, -1 };

static void tf_world(int k, float* wx, float* wy)
{
	float x, y, w, h, a;
	sel_transform_get(&x, &y, &w, &h, &a);
	float lx = HX[k] * w / 2, ly = HY[k] * h / 2;
	if (k == 8) ly -= 18 / ZOOMS[zoomIdx];
	float r = a * (float)M_PI / 180, ca = cosf(r), sa = sinf(r);
	*wx = x + w / 2 + lx * ca - ly * sa;
	*wy = y + h / 2 + lx * sa + ly * ca;
}

static void tf_screen(int k, float* sx, float* sy)
{
	float wx, wy;
	tf_world(k, &wx, &wy);
	*sx = canvas_sx() + wx * ZOOMS[zoomIdx];
	*sy = canvas_sy() + wy * ZOOMS[zoomIdx];
}

static int tf_hit(int px, int py)
{
	static const int order[9] = { 8, 0, 1, 2, 3, 4, 5, 6, 7 };   // rotate dot first, then corners
	for (int i = 0; i < 9; i++)
	{
		float hx, hy;
		tf_screen(order[i], &hx, &hy);
		if (fabsf(px - hx) <= 11 && fabsf(py - hy) <= 11) return order[i];
	}
	return -1;
}

// Is a canvas point inside the (possibly rotated) transform box?
static bool tf_inside(float fx, float fy)
{
	float x, y, w, h, a;
	sel_transform_get(&x, &y, &w, &h, &a);
	float r = a * (float)M_PI / 180, ca = cosf(r), sa = sinf(r);
	float dx = fx - (x + w / 2), dy = fy - (y + h / 2);
	float lx = dx * ca + dy * sa, ly = -dx * sa + dy * ca;
	return fabsf(lx) <= w / 2 + 0.5f && fabsf(ly) <= h / 2 + 0.5f;
}

static void tf_begin(int k)
{
	tfHandle = k;
	tfDrag = k < 4 ? TF_CORNER : k < 8 ? TF_EDGE : TF_ROTATE;
	if (k < 4) tf_world((k + 2) % 4, &tfFixX, &tfFixY);           // opposite corner stays put
	else if (k < 8) tf_world(4 + (k - 4 + 2) % 4, &tfFixX, &tfFixY); // opposite edge stays put
}

static float crisp_scale(float s)
{
	if (s >= 1) return fmaxf(1, roundf(s));
	return 1.0f / fmaxf(1, roundf(1 / s));
}

static void tf_drag(float fx, float fy, bool zr, bool crisp)
{
	float x, y, w, h, a;
	int sw, sh;
	sel_transform_get(&x, &y, &w, &h, &a);
	sel_source_size(&sw, &sh);
	float r = a * (float)M_PI / 180, ca = cosf(r), sa = sinf(r);
	if (tfDrag == TF_ROTATE)
	{
		float cx = x + w / 2, cy = y + h / 2;
		float ang = atan2f(fx - cx, -(fy - cy)) * 180 / (float)M_PI;   // 0 = handle straight up
		if (zr) ang = roundf(ang / 15) * 15;                             // ZR: 15-degree steps
		sel_transform(&doc, x, y, w, h, ang, false);
		return;
	}
	// stylus relative to the fixed point, in the box's own (unrotated) axes
	float dx = fx - tfFixX, dy = fy - tfFixY;
	float vx = dx * ca + dy * sa, vy = -dx * sa + dy * ca;
	float dirx = HX[tfHandle], diry = HY[tfHandle];
	float nw = w, nh = h;
	if (tfDrag == TF_CORNER)
	{
		nw = fmaxf(1, vx * dirx);
		nh = fmaxf(1, vy * diry);
		if (!zr)
		{
			// proportional (no warping): one scale for both directions
			float s = fmaxf(nw / sw, nh / sh);
			if (crisp) s = crisp_scale(s);
			nw = sw * s;
			nh = sh * s;
		}
		else if (crisp)
		{
			nw = sw * crisp_scale(nw / sw);
			nh = sh * crisp_scale(nh / sh);
		}
	}
	else if (dirx != 0)
	{
		nw = fmaxf(1, vx * dirx);
		if (crisp) nw = sw * crisp_scale(nw / sw);
	}
	else
	{
		nh = fmaxf(1, vy * diry);
		if (crisp) nh = sh * crisp_scale(nh / sh);
	}
	// new centre: from the fixed point, half the new size toward the dragged handle
	float ox = dirx * nw / 2, oy = diry * nh / 2;
	float ncx = tfFixX + ox * ca - oy * sa, ncy = tfFixY + ox * sa + oy * ca;
	sel_transform(&doc, ncx - nw / 2, ncy - nh / 2, nw, nh, a, false);
}

static void tf_end(void)
{
	float x, y, w, h, a;
	sel_transform_get(&x, &y, &w, &h, &a);
	// smoother rotation once let go; on an Old 3DS only for small pieces (it is 64x the work)
	if (a != 0 && (isNew3DS || w * h <= 48 * 48)) sel_transform(&doc, x, y, w, h, a, true);
	if (tool == TOOL_TEXT)
	{
		int sw, sh;
		sel_source_size(&sw, &sh);
		textScale = (int)fmaxf(1, roundf(w / sw));
	}
}

static void set_tool(Tool t)
{
	if (t == tool)
		return;
	curve_finish();
	if (tool == TOOL_SELECT)
		finish_floating();          // the selection itself stays, like other editors
	if (tool == TOOL_TEXT)
		text_place();
	if (tool == TOOL_STAMP || t == TOOL_STAMP || t == TOOL_TEXT)
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
	animDirty = true;
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

static int luma(uint32_t c)
{
	return (int)(((c >> 24) & 255) * 299 + ((c >> 16) & 255) * 587 + ((c >> 8) & 255) * 114) / 1000;
}

// Shading: every color steps one shade lighter or darker.
// Swatch mode: the closest swatch (by color) that is actually lighter/darker - works for any
// color and any palette order, and stays within the same color family.
// Brightness mode: the same hue with its brightness nudged up or down.
static void build_shade_map(void)
{
	for (int i = 0; i < 256; i++) shadeMap[i] = i;
	int count = doc.colorCount;
	for (int v = 1; v <= count; v++)
	{
		uint32_t c = doc.colors[v], to = c;
		if (shadeBrightness)
		{
			float h, sa, val;
			rgba_to_hsv(c, &h, &sa, &val);
			val += shadeDarker ? -0.12f : 0.12f;
			if (val > 1 && !shadeDarker) { sa -= val - 1; val = 1; }   // past full brightness: wash toward white
			val = val < 0 ? 0 : val > 1 ? 1 : val;
			sa = sa < 0 ? 0 : sa;
			to = hsv_to_rgba(h, sa, val);
		}
		else
		{
			int lc = luma(c);
			long best = -1;
			for (int k = 0; k < DOC_PALETTE_SIZE; k++)
			{
				uint32_t sw = doc.swatches[k];
				int ls = luma(sw);
				if (shadeDarker ? ls >= lc - 2 : ls <= lc + 2) continue;   // must really be lighter/darker
				long dr = (long)((sw >> 24) & 255) - ((c >> 24) & 255);
				long dg = (long)((sw >> 16) & 255) - ((c >> 16) & 255);
				long db = (long)((sw >> 8) & 255) - ((c >> 8) & 255);
				long dist = dr * dr * 3 + dg * dg * 4 + db * db * 2;
				if (best < 0 || dist < best) { best = dist; to = sw; }
			}
		}
		if (to != c)
			shadeMap[v] = doc_color_index(&doc, to, false);
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
		rows[n++] = OPT_MIRRORON; rows[n++] = OPT_MIRROR; rows[n++] = OPT_AXISPOS; rows[n++] = OPT_AXISRESET; break;
	case TOOL_ERASER:
		rows[n++] = OPT_ERASEMODE; rows[n++] = OPT_SIZE; rows[n++] = OPT_SHAPE; break;
	case TOOL_FILL:
		rows[n++] = OPT_FILLMODE; break;
	case TOOL_SHADE:
		rows[n++] = OPT_SHADEDIR; rows[n++] = OPT_SHADEMODE; rows[n++] = OPT_SIZE; rows[n++] = OPT_SHAPE; break;
	case TOOL_LINE:
		rows[n++] = OPT_LINEMODE; rows[n++] = OPT_SIZE; rows[n++] = OPT_SHAPE; break;
	case TOOL_RECT:
	case TOOL_ELLIPSE:
		rows[n++] = OPT_STYLE; rows[n++] = OPT_SIZE; break;
	case TOOL_SELECT:
	{
		rows[n++] = OPT_SELMODE;
		rows[n++] = OPT_SELOP;
		if (selMode == SELMODE_WAND) { rows[n++] = OPT_WANDALL; rows[n++] = OPT_TOLERANCE; rows[n++] = OPT_SAMPLE; }
		rows[n++] = OPT_CRISP;
		static const OptKind acts[] = { OPT_TRANSFORM, OPT_APPLY, OPT_SELALL, OPT_DESELECT, OPT_INVERT, OPT_COPY, OPT_CUT,
		                                OPT_PASTE, OPT_DELETE, OPT_FLIPH, OPT_FLIPV, OPT_ROTATE, OPT_OUTLINE, OPT_CROP,
		                                OPT_SAVESTAMP };
		for (size_t i = 0; i < sizeof(acts) / sizeof(acts[0]); i++)
			rows[n++] = acts[i];
		return n;
	}
	case TOOL_STAMP:
		rows[n++] = OPT_STAMP; rows[n++] = OPT_CRISP; rows[n++] = OPT_APPLY;
		return n;
	case TOOL_TEXT:
		rows[n++] = OPT_FONT; rows[n++] = OPT_TSCALE; rows[n++] = OPT_TSPACING; rows[n++] = OPT_TOUTLINE;
		rows[n++] = OPT_CRISP; rows[n++] = OPT_TEXTEDIT; rows[n++] = OPT_TEXTPLACE;
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
	case OPT_CRISP: return "Crisp sizes";
	case OPT_ERASEMODE: return "Mode";
	case OPT_SHADEMODE: return "Using";
	case OPT_LINEMODE: return "Mode";
	case OPT_AXISPOS: return "Axis position";
	case OPT_AXISRESET: return "Reset axis to center";
	case OPT_SELOP: return "Combine";
	case OPT_TOLERANCE: return "Tolerance";
	case OPT_SAMPLE: return "Sample";
	case OPT_INVERT: return "Invert selection";
	case OPT_TRANSFORM: return "Transform (handles)";
	case OPT_APPLY: return "Apply / place";
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
	case OPT_FONT: return "Font";
	case OPT_TSCALE: return "Size";
	case OPT_TSPACING: return "Spacing";
	case OPT_TOUTLINE: return "Outline";
	case OPT_TEXTEDIT: return "Edit text...";
	case OPT_TEXTPLACE: return "Place text";
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
	case OPT_SHADEDIR: snprintf(out, n, "%s", shadeDarker ? "Darker" : "Lighter"); break;
	case OPT_SHADEMODE: snprintf(out, n, "%s", shadeBrightness ? "Brightness" : "Nearest swatch"); break;
	case OPT_LINEMODE: snprintf(out, n, "%s", lineCurve ? "Curve (drag, then bend)" : "Straight"); break;
	case OPT_SELMODE: snprintf(out, n, "%s", SELMODE_NAMES[selMode]); break;
	case OPT_WANDALL: snprintf(out, n, "%s", wandAll ? "All of that color" : "Connected only"); break;
	case OPT_FONT: snprintf(out, n, "%s", FONTS[textFont].name); break;
	case OPT_TSCALE: snprintf(out, n, "%dx", textScale); break;
	case OPT_TSPACING: snprintf(out, n, "%+d px", textSpacing); break;
	case OPT_TOUTLINE:
		if (textOutline < 0) snprintf(out, n, "Off");
		else snprintf(out, n, "Swatch %d #%06lX", textOutline + 1, (unsigned long)(doc.swatches[textOutline] >> 8));
		break;
	case OPT_MIRROR: snprintf(out, n, "%s", MIRROR_NAMES[mirrorMode]); break;
	case OPT_MIRRORON: snprintf(out, n, "%s", mirrorOn ? "On" : "Off"); break;
	case OPT_CRISP:
		snprintf(out, n, "%s", (optTool == TOOL_TEXT ? textCrisp : tfCrisp) ? "On (1x, 2x, 3x...)" : "Off (any size)");
		break;
	case OPT_SELOP: snprintf(out, n, "%s", selOp == SELOP_NEW ? "New selection" : selOp == SELOP_ADD ? "Add to selection" : "Subtract"); break;
	case OPT_TOLERANCE: snprintf(out, n, "%d%%%s", wandTol, wandTol ? "" : " (exact color)"); break;
	case OPT_SAMPLE: snprintf(out, n, "%s", wandSampleAll ? "All visible layers" : "Current layer"); break;
	case OPT_ERASEMODE: snprintf(out, n, "%s", eraserLine ? "Line (drag)" : "Freehand"); break;
	case OPT_AXISPOS:
		if (!mirrorAX2 && !mirrorAY2) snprintf(out, n, "Center (touch canvas to move)");
		else snprintf(out, n, "x %.1f  y %.1f", (mirrorAX2 ? mirrorAX2 : doc.w) / 2.0f, (mirrorAY2 ? mirrorAY2 : doc.h) / 2.0f);
		break;
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
	case OPT_FONT: textFont = (textFont + FONT_COUNT + dir) % FONT_COUNT; break;
	case OPT_TSCALE:
		textScale = textScale + dir < 1 ? 1 : textScale + dir > TEXT_MAX_SCALE ? TEXT_MAX_SCALE : textScale + dir;
		text_apply_scale();
		break;
	case OPT_TSPACING: textSpacing = textSpacing + dir < -1 ? -1 : textSpacing + dir > 4 ? 4 : textSpacing + dir; break;
	case OPT_TOUTLINE: textOutline = (textOutline + 1 + DOC_PALETTE_SIZE + 1 + dir) % (DOC_PALETTE_SIZE + 1) - 1; break;
	case OPT_MIRROR: // Left-right, Top-bottom, Both ("Off" is the On/Off row)
		mirrorMode = (Mirror)((mirrorMode - 1 + 3 + dir) % 3 + 1);
		break;
	case OPT_STAMP: if (stampCount) stampSel = (stampSel + stampCount + dir) % stampCount; break;
	case OPT_MIRRORON: mirrorOn = !mirrorOn; break;
	case OPT_CRISP: if (optTool == TOOL_TEXT) textCrisp = !textCrisp; else tfCrisp = !tfCrisp; break;
	case OPT_SELOP: selOp = (SelOp)((selOp + 3 + dir) % 3); break;
	case OPT_TOLERANCE: wandTol = wandTol + dir * 5 < 0 ? 0 : wandTol + dir * 5 > 100 ? 100 : wandTol + dir * 5; break;
	case OPT_SAMPLE: wandSampleAll = !wandSampleAll; break;
	case OPT_ERASEMODE: eraserLine = !eraserLine; break;
	case OPT_SHADEMODE: shadeBrightness = !shadeBrightness; break;
	case OPT_LINEMODE: lineCurve = !lineCurve; break;
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
	bool edits = k == OPT_CUT || k == OPT_DELETE || k == OPT_FLIPH || k == OPT_FLIPV || k == OPT_ROTATE ||
	             k == OPT_OUTLINE || (k == OPT_PASTE && sel_has_clip());
	if (edits && !layer_editable())
		return;
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
	case OPT_TEXTEDIT:
		if (textOn) text_edit();
		else set_status("Tap the canvas to type new text");
		break;
	case OPT_TEXTPLACE: text_place(); break;
	case OPT_INVERT: sel_invert(&doc); break;
	case OPT_AXISRESET: mirrorAX2 = mirrorAY2 = 0; break;
	case OPT_TRANSFORM:
		if (!layer_editable()) break;
		if (!sel_active()) sel_all(&doc);           // nothing selected: transform the whole layer
		if (!sel_floating()) sel_lift(&doc);
		set_status("Corners: size  Edges: stretch  Top dot: rotate  B: apply");
		break;
	case OPT_APPLY:
		if (tool == TOOL_TEXT) text_place();
		else { finish_floating(); if (tool == TOOL_STAMP) sel_clear(&doc); }
		break;
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
	if (kDown & KEY_B) { mode = MODE_DRAW; text_refresh(); return; }
	if (kDown & KEY_A)
	{
		mode = MODE_DRAW;
		if (n && rows[optSel] >= OPT_ACT_FIRST)
			run_option_action(rows[optSel]);
		else
			text_refresh();
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
		snprintf(out, n, "%d %s%s", b->size, SHAPE_NAMES[b->shape], eraserLine ? " line" : ""); break;
	case TOOL_LINE:
		snprintf(out, n, "%d %s%s", b->size, SHAPE_NAMES[b->shape], lineCurve ? " curve" : ""); break;
	case TOOL_RECT:
	case TOOL_ELLIPSE:
		if (shapeFilled[t]) snprintf(out, n, "Filled");
		else snprintf(out, n, "Outline %d", b->size);
		break;
	case TOOL_FILL: snprintf(out, n, "%s", fillMode == FILL_AREA ? "Area" : fillMode == FILL_REPLACE ? "Replace" : "Gradient"); break;
	case TOOL_SHADE: snprintf(out, n, "%s %d%s", shadeDarker ? "Darker" : "Lighter", b->size, shadeBrightness ? " bright" : ""); break;
	case TOOL_SELECT:
		if (sel_active()) { Rect r = sel_rect(); snprintf(out, n, "%s %dx%d", SELMODE_NAMES[selMode], r.w, r.h); }
		else snprintf(out, n, "%s", SELMODE_NAMES[selMode]);
		break;
	case TOOL_STAMP: snprintf(out, n, "%s", stampCount ? stampNames[stampSel] : "none"); break;
	case TOOL_TEXT: snprintf(out, n, "%s %dx", FONTS[textFont].name, textScale); break;
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
	confirmBack = mode == MODE_HOME ? MODE_HOME : MODE_MENU;
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

static void open_project(const char* name)
{
	char path[160];
	project_path(path, sizeof(path), name);
	sel_reset();
	mirrorAX2 = mirrorAY2 = 0;
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
	mirrorAX2 = mirrorAY2 = 0;
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
	case ACT_IMPORT_PROJECT: import_as(importHow); break;
	case ACT_DELETE_GAL: gallery_delete(); break;
	default: break;
	}
}

// ---- menus ---------------------------------------------------------------------

enum { MI_BACK, MI_HOME, MI_SAVE, MI_SAVEAS, MI_IMPORT, MI_EXPORT, MI_SIZE, MI_SHIFT, MI_COLORS, MI_VIEW,
       MI_LINKED, MI_EXIT, MENU_COUNT };
static const char* MENU_ITEMS[MENU_COUNT] = { "Back to canvas", "Home (new / gallery)", "Save", "Save as...",
                                              "Import (PNG, GIF, palette)...", "Export (PNG / sheet / GIF)...", "Canvas size...", "Shift layer (wrap)...",
                                              "Colors...", "View...", "Linked swatches", "Exit" };
#define MENU_LINKED MI_LINKED

// ---- PNG export ------------------------------------------------------------------

typedef struct { int mult, fixed; } ExportSize;
static const ExportSize EXPORT_SIZES[] = {
	{ 1, 0 }, { 2, 0 }, { 3, 0 }, { 4, 0 }, { 6, 0 }, { 8, 0 },
	{ 0, 16 }, { 0, 24 }, { 0, 32 }, { 0, 48 }, { 0, 64 }, { 0, 128 }, { 0, 256 },
};
#define EXPORT_SIZE_COUNT (int)(sizeof(EXPORT_SIZES) / sizeof(EXPORT_SIZES[0]))
#define EXPORT_HEAD 5                        // format, frames, layout, padding, trim rows
#define EXPORT_ROWS (EXPORT_HEAD + EXPORT_SIZE_COUNT + 2)  // + "Recommended" + sizes + "Export"
static bool exportOn[EXPORT_SIZE_COUNT];
static int exportSel;
enum { EXF_PNG, EXF_SHEET, EXF_FRAMES, EXF_GIF, EXF_PNG8, EXF_COUNT };
static const char* EXF_NAMES[EXF_COUNT] = { "PNG (this frame)", "Sprite sheet PNG", "PNG per frame", "Animated GIF",
                                            "Indexed PNG (this frame)" };
static const int SHEET_PADS[] = { 0, 1, 2, 4, 8 };
static const char* LAYOUT_NAMES[SHEET_LAYOUT_COUNT] = { "One row", "One column", "Grid" };
static int exportFmt, exportTag = -1, exportLayout = SHEET_ROW;   // exportTag: -1 all frames, -2 each tag
static bool export_one_frame(void) { return exportFmt == EXF_PNG || exportFmt == EXF_PNG8; }

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
static bool export_dims_for(const Doc* d, int i, int* w, int* h, bool* even)
{
	const ExportSize* e = &EXPORT_SIZES[i];
	if (e->mult)
	{
		*w = d->w * e->mult;
		*h = d->h * e->mult;
		*even = true;
	}
	else
	{
		int longest = d->w > d->h ? d->w : d->h;
		*w = (d->w * e->fixed + longest / 2) / longest;
		*h = (d->h * e->fixed + longest / 2) / longest;
		if (*w < 1) *w = 1;
		if (*h < 1) *h = 1;
		*even = e->fixed % longest == 0;
	}
	if (exportFmt != EXF_PNG && !*even) return false;   // everything else: whole-number scales only
	if (exportFmt == EXF_SHEET)
	{
		// checked without trimming (never bigger with it, and it would composite every frame here)
		SheetOpts o = { exportLayout, *w / d->w, sheetPad, false };
		int sw, sh;
		return anim_sheet_size(d, exportTag, &o, &sw, &sh);
	}
	if (exportFmt == EXF_GIF) return *w <= GIF_MAX_DIM && *h <= GIF_MAX_DIM;
	return *w <= PNG_MAX_DIM && *h <= PNG_MAX_DIM;
}

static bool export_dims(int i, int* w, int* h, bool* even) { return export_dims_for(&doc, i, w, h, even); }

static void run_export_doc(const Doc* d, const char* base)
{
	ensure_dirs();
	int done = 0, failed = 0;
	const char* lastErr = NULL;
	for (int i = 0; i < EXPORT_SIZE_COUNT; i++)
	{
		int w, h;
		bool even;
		if (!exportOn[i] || !export_dims_for(d, i, &w, &h, &even)) continue;
		// "each tag" exports one file per tag (or all frames if there are none)
		bool each = exportTag == -2 && !export_one_frame() && d->tagCount > 0;
		int t0 = each ? 0 : exportTag < -1 ? -1 : exportTag, t1 = each ? d->tagCount - 1 : t0;
		for (int tag = t0; tag <= t1; tag++)
		{
		char path[256], stem[DOC_NAME_MAX + TAG_NAME_MAX + 2];
		int scale = w / d->w;
		if (tag >= 0 && tag < d->tagCount) snprintf(stem, sizeof(stem), "%s_%s", base, d->tags[tag].name);
		else snprintf(stem, sizeof(stem), "%s", base);
		const char* err;
		switch (exportFmt)
		{
		case EXF_SHEET:
		{
			char img[DOC_NAME_MAX + TAG_NAME_MAX + 24];
			snprintf(img, sizeof(img), "%s_sheet_%dx.png", stem, scale);
			snprintf(path, sizeof(path), EXPORT_DIR "/%s", img);
			SheetOpts o = { exportLayout, scale, sheetPad, sheetTrim };
			err = anim_export_sheet(d, tag, &o, path);
			if (!err)
			{
				snprintf(path, sizeof(path), EXPORT_DIR "/%s_sheet_%dx.json", stem, scale);
				err = anim_export_sheet_json(d, tag, &o, img, path);
			}
			break;
		}
		case EXF_PNG8:
			snprintf(path, sizeof(path), EXPORT_DIR "/%s_%dx%d_indexed.png", base, w, h);
			err = png_export_indexed(d, d->curFrame, scale, path);
			if (err) lastErr = err;
			break;
		case EXF_FRAMES:
			snprintf(path, sizeof(path), EXPORT_DIR "/%s_%dx", stem, scale);
			err = anim_export_frames(d, tag, scale, path);
			break;
		case EXF_GIF:
			snprintf(path, sizeof(path), EXPORT_DIR "/%s_%dx.gif", stem, scale);
			err = gif_export(d, tag, scale, path);
			break;
		default:
			snprintf(path, sizeof(path), EXPORT_DIR "/%s_%dx%d.png", base, w, h);
			err = png_export(d, d->curFrame, w, h, path);
			break;
		}
		if (err) failed++;
		else done++;
		}
	}
	char msg[96];
	const char* what = exportFmt == EXF_GIF ? "GIF" : exportFmt == EXF_SHEET ? "sheet" : exportFmt == EXF_FRAMES ? "frame set"
	                 : exportFmt == EXF_PNG8 ? "indexed PNG" : "PNG";
	if (failed && lastErr) snprintf(msg, sizeof(msg), "%.90s", lastErr);
	else if (failed) snprintf(msg, sizeof(msg), "Exported %d %s, %d failed", done, what, failed);
	else if (done) snprintf(msg, sizeof(msg), "Exported %d %s to /3ds/pixel-crow/export", done, what);
	else snprintf(msg, sizeof(msg), "No sizes ticked");
	set_status(msg);
}

static void run_export(void) { run_export_doc(&doc, doc.name[0] ? doc.name : "untitled"); }

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
			case MI_BACK: mode = MODE_DRAW; break;
			case MI_HOME: open_home(true); break;
			case MI_SAVE:
				if (doc.name[0] && strcmp(doc.name, "_autosave")) { save_as_name(doc.name); mode = MODE_DRAW; }
				else save_as();
				break;
			case MI_SAVEAS: save_as(); break;
			case MI_IMPORT: scan_imports(); importSel = 0; mode = MODE_IMPORT; break;
			case MI_EXPORT: mode = MODE_EXPORT; exportSel = 0; break;
			case MI_SIZE:
			{
				int w = ask_number("New width (1-320)", doc.w, DOC_MAX_W);
				int h = w ? ask_number("New height (1-240)", doc.h, DOC_MAX_H) : 0;
				if (w && h) { resizeW = w; resizeH = h; mode = MODE_ANCHOR; }
				break;
			}
			case MI_SHIFT:
				if (!layer_editable()) break;
				shift_begin(&doc);
				shiftX = shiftY = 0;
				mode = MODE_SHIFT;
				break;
			case MI_COLORS: colorsSel = 0; mode = MODE_COLORS; break;
			case MI_VIEW: viewSel = 0; mode = MODE_VIEW; break;
			case MENU_LINKED:
				doc.linkedSwatches = !doc.linkedSwatches;
				doc.dirty = true;
				set_status(doc.linkedSwatches ? "Linked: editing a swatch recolors the drawing"
				                              : "Unlinked: swatches are just saved colors");
				break;
			case MI_EXIT:
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
	{
		list_nav(kDown, &exportSel, EXPORT_ROWS);
		if (kDown & KEY_B) mode = MODE_MENU;
		int dir = (kDown & (KEY_DRIGHT | KEY_A)) ? 1 : (kDown & KEY_DLEFT) ? -1 : 0;
		if (exportSel < EXPORT_HEAD)
		{
			if (!dir) break;
			if (exportSel == 0) exportFmt = (exportFmt + EXF_COUNT + dir) % EXF_COUNT;
			else if (exportSel == 1)
			{
				// all frames, every tag as separate files, then each single tag
				int n = doc.tagCount ? doc.tagCount + 2 : 1;
				int k = exportTag == -1 ? 0 : exportTag == -2 ? 1 : exportTag + 2;
				k = (k + n + dir) % n;
				exportTag = k == 0 ? -1 : k == 1 ? -2 : k - 2;
			}
			else if (exportSel == 2) exportLayout = (exportLayout + SHEET_LAYOUT_COUNT + dir) % SHEET_LAYOUT_COUNT;
			else if (exportSel == 3)
			{
				int k;
				for (k = 0; k < 5 && SHEET_PADS[k] != sheetPad; k++) {}
				sheetPad = SHEET_PADS[(k % 5 + 5 + dir) % 5];
			}
			else sheetTrim = !sheetTrim;
		}
		else if (kDown & KEY_A)
		{
			int r = exportSel - EXPORT_HEAD;
			if (r == 0) export_recommended();
			else if (exportSel == EXPORT_ROWS - 1) { run_export(); mode = MODE_DRAW; }
			else exportOn[r - 1] = !exportOn[r - 1];
		}
		break;
	}
	case MODE_CONFIRM:
		if (kDown & KEY_A) run_action(confirmAction);
		else if (kDown & KEY_B) mode = confirmBack;
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

// Small previews for the layer panel (borrow the gallery's thumbnail slots 0..15).
static bool layerThumbsDirty = true;
static int layerThumbW[DOC_MAX_LAYERS], layerThumbH[DOC_MAX_LAYERS];

static void make_layer_thumbs(void)
{
	static uint32_t th[PXD_THUMB * PXD_THUMB];
	int w = doc.w, h = doc.h;
	int up = PXD_THUMB / (w > h ? w : h), tw, tht;
	if (up >= 1) { tw = w * up; tht = h * up; }
	else if (w >= h) { tw = PXD_THUMB; tht = h * PXD_THUMB / w; }
	else { tht = PXD_THUMB; tw = w * PXD_THUMB / h; }
	if (tw < 1) tw = 1;
	if (tht < 1) tht = 1;
	for (int i = 0; i < doc.layerCount; i++)
	{
		if (doc.layers[i].isFolder) continue;
		const uint8_t* cel = doc.layers[i].cels[doc.curFrame];
		for (int y = 0; y < tht; y++)
			for (int x = 0; x < tw; x++)
				th[y * tw + x] = doc_color(&doc, cel[(y * h / tht) * w + x * w / tw]);
		gfx_thumb_set(i, th, tw, tht);
		layerThumbW[i] = tw;
		layerThumbH[i] = tht;
	}
	layerThumbsDirty = false;
}

static void open_layers(void)
{
	layerThumbsDirty = true;
	finish_pending();
	// the cursor starts on the current layer, so it has to be visible in the list
	int fo = doc.layers[doc.curLayer].folder >= 0 ? doc_layer_index(&doc, doc.layers[doc.curLayer].folder) : -1;
	if (fo >= 0) doc.layers[fo].collapsed = false;
	layerCursor = doc.curLayer;
	mode = MODE_LAYERS;
}

static void run_layer_action(int a)
{
	int i = layerCursor;
	Layer* L = &doc.layers[i];
	int r = -1;
	bool structural = a <= 7;
	if ((a == 0 || (a == 2 && !L->isFolder)) && !room_for((size_t)doc.w * doc.h * doc.frameCount))
		return;
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
	case 8: hist_push_meta(&doc, 0); L->visible = !L->visible; doc.dirty = true; break;
	case 9: hist_push_meta(&doc, 0); L->locked = !L->locked; doc.dirty = true; break;
	case 10:
	{
		char nm[DOC_NAME_MAX];
		if (ask_text("Layer name", L->name, nm, LAYER_NAME_MAX))
		{
			hist_push_meta(&doc, 0);
			snprintf(L->name, LAYER_NAME_MAX, "%.15s", nm);
			doc.dirty = true;
		}
		break;
	}
	case 11:
		if (L->isFolder) { L->collapsed = !L->collapsed; doc.dirty = true; }
		else set_status("Not a folder");
		break;
	}
	layerThumbsDirty = true;
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
		hist_push_meta(&doc, 0x100 + L->id);          // one undo step per run of presses
		L->opacity = o < 0 ? 0 : o > 255 ? 255 : o;
		doc.dirty = true;
		after_doc_change();
	}
	if (kDown & KEY_X) { hist_push_meta(&doc, 0); L->visible = !L->visible; doc.dirty = true; after_doc_change(); }
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
		const char* ext = n > 4 ? e->d_name + n - 4 : "";
		if (n < sizeof(importFiles[0]) && (!strcasecmp(ext, ".png") || !strcasecmp(ext, ".gif") ||
		                                   !strcasecmp(ext, ".hex") || !strcasecmp(ext, ".gpl")))
			snprintf(importFiles[importCount++], sizeof(importFiles[0]), "%s", e->d_name);
	}
	closedir(dir);
	qsort(importFiles, importCount, sizeof(importFiles[0]), (int (*)(const void*, const void*))strcasecmp);
}

#define IMPORT_AS_COUNT 5
static const char* IMPORT_AS[IMPORT_AS_COUNT] = { "As new project", "As new layer", "As stamp", "As swatches (palette)",
                                                  "As animation (sprite sheet)..." };
enum { IMP_PROJECT, IMP_LAYER, IMP_STAMP, IMP_SWATCHES, IMP_SHEET, IMP_GIF };

static bool import_has_ext(const char* ext)
{
	size_t n = strlen(importFiles[importSel]);
	return n > 4 && !strcasecmp(importFiles[importSel] + n - 4, ext);
}

// The selected file's name without extension, made safe for a project name.
static void import_base_name(char* base, size_t n)
{
	snprintf(base, n, "%s", importFiles[importSel]);
	char* dot = strrchr(base, '.');
	if (dot) *dot = 0;
	for (char* c = base; *c; c++)
		if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-')) *c = '_';
}

// An import just replaced the whole document (undo history already cleared).
static void import_replaced_doc(const char* msg)
{
	sel_reset();
	mirrorAX2 = mirrorAY2 = 0;
	import_base_name(doc.name, sizeof(doc.name));
	doc.dirty = true;
	remove(AUTOSAVE_PATH);
	layerThumbsDirty = true;
	view_fit();
	set_status(msg);
	after_doc_change();
}

// .hex / .gpl: the file's colors become the swatches (one undo step; the drawing is untouched)
static void import_palette_file(void)
{
	char path[200];
	snprintf(path, sizeof(path), IMPORT_DIR "/%s", importFiles[importSel]);
	uint32_t cols[DOC_PALETTE_SIZE];
	int n = palette_load(path, cols, DOC_PALETTE_SIZE);
	if (!n) { set_status("No colors found in that file"); return; }
	hist_push_meta(&doc, 0);
	memcpy(doc.swatches, cols, n * sizeof(uint32_t));
	doc.dirty = true;
	char msg[64];
	snprintf(msg, sizeof(msg), "Set %d swatch%s from the palette file", n, n == 1 ? "" : "es");
	set_status(msg);
}

// how: IMP_* (project, layer, stamp, swatches, sprite sheet as frames, GIF as frames)
static void import_as(int how)
{
	char path[200];
	snprintf(path, sizeof(path), IMPORT_DIR "/%s", importFiles[importSel]);
	if (how == IMP_GIF)
	{
		bool cut = false;
		const char* gerr = import_gif_doc(&doc, path, &cut);
		if (gerr) { set_status(gerr); return; }
		import_replaced_doc(cut ? "Imported the first 64 frames of the GIF" : "Imported GIF as an animation");
		return;
	}
	uint32_t* rgba;
	int w, h;
	bool shrunk = false;
	// shrink while reading: big images never sit in memory at full size
	int maxW = how == IMP_LAYER ? doc.w : DOC_MAX_W, maxH = how == IMP_LAYER ? doc.h : DOC_MAX_H;
	if (how == IMP_SHEET) { maxW = DOC_MAX_W * importCols; maxH = DOC_MAX_H * importRows; }
	const char* err = import_read_png(path, maxW, maxH, &rgba, &w, &h, &shrunk);
	if (err) { set_status(err); return; }
	char base[DOC_NAME_MAX];
	import_base_name(base, sizeof(base));
	if (how == IMP_SHEET)
	{
		err = import_sheet_doc(&doc, rgba, w, h, importCols, importRows);
		free(rgba);
		if (err) { set_status(err); return; }
		char msg[64];
		snprintf(msg, sizeof(msg), "Imported %d frames of %dx%d", doc.frameCount, doc.w, doc.h);
		import_replaced_doc(msg);
		return;
	}
	if (how == 3)
	{
		uint32_t cols[DOC_PALETTE_SIZE];
		int n = import_palette(rgba, w, h, cols, DOC_PALETTE_SIZE);
		if (n) hist_push_meta(&doc, 0);
		memcpy(doc.swatches, cols, n * sizeof(uint32_t));
		doc.dirty = true;
		char msg[64];
		snprintf(msg, sizeof(msg), n ? "Set %d swatches from the image" : "No colors found", n);
		set_status(msg);
	}
	else if (how == 0)
	{
		{
			if (!doc_new(&doc, w, h)) { free(rgba); set_status("Out of memory"); return; }
			sel_reset();
			mirrorAX2 = mirrorAY2 = 0;
			uint8_t* px = import_to_indexed(&doc, rgba, w, h);
			if (px) { memcpy(doc.layers[0].cels[0], px, (size_t)w * h); free(px); }
			snprintf(doc.name, sizeof(doc.name), "%s", base);
			doc.dirty = true;
			remove(AUTOSAVE_PATH);
			view_fit();
			set_status(shrunk ? "Imported (shrunk to fit 320x240)" : "Imported as new project");
		}
	}
	else if (how == 1 && !room_for((size_t)doc.w * doc.h * doc.frameCount)) {}
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
	finish_pending();
	if (!layer_editable()) { mode = MODE_DRAW; return; }
	doc_compact(&doc);  // room for new colors
	size_t n = (size_t)doc.w * doc.h;
	free(adjOrig);
	adjOrig = malloc(n);
	if (!adjOrig) { mode = MODE_DRAW; return; }
	memcpy(adjOrig, doc_pixels(&doc), n);
	adjCount = doc.colorCount;
	hist_begin_cel(&doc);
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
	// one undo step: the whole document when linked swatches may recolor pixels too
	if (doc.linkedSwatches) hist_begin(&doc);
	else hist_push_meta(&doc, 0);
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
		if (doc_recolor(&doc, from, to, n)) after_doc_change();
		hist_commit();
	}
	doc.dirty = true;
	char msg[64];
	snprintf(msg, sizeof(msg), "Ramp: swatches %d-%d", lo + 1, hi + 1);
	set_status(msg);
}

#define COLORS_COUNT 3
static const char* COLORS_ITEMS[COLORS_COUNT] = { "Color ramp (between two swatches)...", "Adjust colors (hue/sat/bright/invert)...",
                                                  "Export swatches (.hex + .gpl palette files)" };

// The 32 swatches as palette files other pixel-art tools read.
static void export_palette(void)
{
	ensure_dirs();
	const char* base = doc.name[0] ? doc.name : "untitled";
	char path[200];
	snprintf(path, sizeof(path), EXPORT_DIR "/%s_palette.hex", base);
	const char* err = palette_save_hex(path, doc.swatches, DOC_PALETTE_SIZE);
	snprintf(path, sizeof(path), EXPORT_DIR "/%s_palette.gpl", base);
	if (!err) err = palette_save_gpl(path, base, doc.swatches, DOC_PALETTE_SIZE);
	set_status(err ? err : "Saved the palette to /3ds/pixel-crow/export");
}

// ---- view -------------------------------------------------------------------------

#define VIEW_ROWS 12
static const char* VIEW_LABELS[VIEW_ROWS] = { "Pixel grid", "Onion skin", "Onion frames", "Onion opacity", "Flip preview",
                                              "Tile preview", "HUD grid", "Iso grid", "Autosave", "Tile drawing",
                                              "Frame bar", "Left-handed" };
static void view_value(int row, char* out, size_t n)
{
	switch (row)
	{
	case 0: snprintf(out, n, "%s", gridOn ? "On" : "Off"); break;
	case 1: snprintf(out, n, "%s", onionOn ? "On" : "Off"); break;
	case 2: snprintf(out, n, "%d before, %d after", onionCount, onionCount); break;
	case 3: snprintf(out, n, "%d%%", onionPct); break;
	case 4: snprintf(out, n, "%s", flipPreview ? "On" : "Off"); break;
	case 5: snprintf(out, n, "%s", tilePreview ? "On (3x3)" : "Off"); break;
	case 6: if (hudGrid) snprintf(out, n, "Every %d px", hudGrid); else snprintf(out, n, "Off"); break;
	case 7: if (isoGrid) snprintf(out, n, "2:1, %d px", isoGrid); else snprintf(out, n, "Off"); break;
	case 8: if (autosaveMin) snprintf(out, n, "Every %d min", autosaveMin); else snprintf(out, n, "Off"); break;
	case 9: snprintf(out, n, "%s", wrapDraw ? "On (strokes wrap)" : "Off"); break;
	case 10: snprintf(out, n, "%s", barHidden ? "Hidden (X: frames)" : "Shown"); break;
	case 11: snprintf(out, n, "%s", inputMap.lefty ? "On (mirrored)" : "Off"); break;
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
	case 0: gridOn = !gridOn; break;
	case 1: onionOn = !onionOn; animDirty = true; break;
	case 2: onionCount = (onionCount + 2 + dir) % 3 + 1; animDirty = true; break;
	case 3: onionPct = onionPct + dir * 5 < 10 ? 10 : onionPct + dir * 5 > 80 ? 80 : onionPct + dir * 5; animDirty = true; break;
	case 4: flipPreview = !flipPreview; break;
	case 5: tilePreview = !tilePreview; break;
	case 6: for (k = 0; k < 4 && HUD[k] != hudGrid; k++); hudGrid = HUD[(k + 4 + dir) % 4]; break;
	case 7: for (k = 0; k < 3 && ISO[k] != isoGrid; k++); isoGrid = ISO[(k + 3 + dir) % 3]; break;
	case 8: for (k = 0; k < 3 && AUTO[k] != autosaveMin; k++); autosaveMin = AUTO[(k + 3 + dir) % 3]; break;
	case 9: wrapDraw = !wrapDraw; break;
	case 10: barHidden = !barHidden; view_clamp(); break;
	case 11:
		inputMap.lefty = !inputMap.lefty;
		set_status(inputMap.lefty ? "While drawing: D-pad and A/B/X/Y swap, L and R swap" : "Buttons back to right-handed");
		break;
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
		if ((kDown & KEY_A) && resizeW * resizeH > doc.w * doc.h &&
		    doc_pixel_bytes(&doc) / ((size_t)doc.w * doc.h) * resizeW * resizeH > PIXEL_BUDGET)
		{
			set_status("Too big for this many layers and frames");
			mode = MODE_DRAW;
		}
		else if (kDown & KEY_A)
		{
			sel_clear(&doc);
			hist_begin(&doc);
			mirrorAX2 = mirrorAY2 = 0;
			if (doc_resize(&doc, resizeW, resizeH, anchorX, anchorY)) { hist_commit(); doc.dirty = true; view_fit(); set_status("Canvas resized"); }
			else { hist_abort(); set_status("Couldn't resize"); }
			after_doc_change();
			mode = MODE_DRAW;
		}
		break;
	case MODE_IMPORT:
		list_nav(kDown, &importSel, importCount);
		if (kDown & KEY_B) mode = MODE_MENU;
		if ((kDown & KEY_A) && importCount)
		{
			if (import_has_ext(".hex") || import_has_ext(".gpl")) { mode = MODE_DRAW; import_palette_file(); }
			else if (import_has_ext(".gif"))
			{
				mode = MODE_DRAW;
				importHow = IMP_GIF;
				if (doc.dirty) confirm(ACT_IMPORT_PROJECT, "Discard unsaved changes?");
				else import_as(IMP_GIF);
			}
			else { importAsSel = 0; mode = MODE_IMPORT_AS; }
		}
		break;
	case MODE_IMPORT_AS:
		list_nav(kDown, &importAsSel, IMPORT_AS_COUNT);
		if (kDown & KEY_B) mode = MODE_IMPORT;
		if (kDown & KEY_A)
		{
			if (importAsSel == IMP_SHEET)
			{
				// how the sheet is cut up: frames across and down
				int c = ask_number("Frames across (1-64)", importCols, DOC_MAX_FRAMES);
				int r = c ? ask_number("Rows of frames (1-64)", importRows, DOC_MAX_FRAMES) : 0;
				if (!c || !r) break;
				if (c * r > DOC_MAX_FRAMES) { set_status("64 frames at most"); break; }
				importCols = c;
				importRows = r;
			}
			mode = MODE_DRAW;
			importHow = importAsSel;
			bool replaces = importAsSel == IMP_PROJECT || importAsSel == IMP_SHEET;
			if (replaces && doc.dirty) confirm(ACT_IMPORT_PROJECT, "Discard unsaved changes?");
			else import_as(importAsSel);
		}
		break;
	case MODE_COLORS:
		list_nav(kDown, &colorsSel, COLORS_COUNT);
		if (kDown & KEY_B) mode = MODE_MENU;
		if (kDown & KEY_A)
		{
			if (colorsSel == 2) { export_palette(); mode = MODE_DRAW; }
			else if (colorsSel == 0)
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
	case MODE_FRAMES: update_frames(kDown); break;
	case MODE_FRAME_ACT:
		list_nav(kDown, &frameActSel, FA_COUNT);
		if (kDown & KEY_B) mode = MODE_FRAMES;
		if (kDown & KEY_A) { mode = MODE_FRAMES; run_frame_action(frameActSel); }
		break;
	default: break;
	}
	(void)kHeld;
}

// ---- frames (animation) ----------------------------------------------------------------

static bool bar_shown(void) { return doc.frameCount > 1 && mode == MODE_DRAW && !barHidden; }

static void play_restart(void)
{
	if (playTag >= doc.tagCount) playTag = -1;
	playN = anim_sequence(&doc, playTag, playSeq);
	playPos = 0;
	playNext = osGetTime() + doc_frame_ms(&doc, playSeq[0]);
	playShown = -1;
}

static bool anim_playing(void) { return playOn && doc.frameCount > 1 && playShown >= 0; }

static void update_play(void)
{
	if (!playOn || doc.frameCount < 2) { playShown = -1; return; }
	if (playN == 0) play_restart();
	u64 now = osGetTime();
	while (now >= playNext)
	{
		playPos = (playPos + 1) % playN;
		playNext += doc_frame_ms(&doc, playSeq[playPos]);
		if (now > playNext + 500) playNext = now;   // don't race to catch up after a pause
	}
	int f = playSeq[playPos];
	if (f >= doc.frameCount) { play_restart(); f = playSeq[0]; }
	if (f != playShown) { gfx_play_set(&doc, f); playShown = f; }
}

// Called once per loop: refresh the onion skin and playback after edits (not mid-stroke).
static void update_anim(void)
{
	if (animDirty && touchTarget == TOUCH_NONE)
	{
		if (onionOn && doc.frameCount > 1) gfx_onion_rebuild(&doc, onionCount, onionCount, onionPct * 255 / 100);
		else gfx_onion_clear();
		frameThumbOk[doc.curFrame] = false;
		playN = 0;           // frames or tags may have changed
		animDirty = false;
	}
	update_play();
}

static void set_frame(int f)
{
	if (f < 0 || f >= doc.frameCount || f == doc.curFrame) return;
	finish_pending();
	doc.curFrame = f;
	layerThumbsDirty = true;
	after_doc_change();
}

static void frame_thumbs_reset(void) { memset(frameThumbOk, 0, sizeof(frameThumbOk)); }

static void open_frames(void)
{
	finish_pending();
	frame_thumbs_reset();
	mode = MODE_FRAMES;
}

// Room for one more frame of every layer?
static bool frame_fits(void)
{
	size_t one = 0;
	for (int i = 0; i < doc.layerCount; i++)
		if (!doc.layers[i].isFolder) one += (size_t)doc.w * doc.h;
	if (doc.frameCount >= DOC_MAX_FRAMES) { set_status("64 frames max"); return false; }
	if (doc_pixel_bytes(&doc) + one > PIXEL_BUDGET) { set_status("Out of room: fewer layers or a smaller canvas"); return false; }
	return true;
}

static void frame_add(bool copy)
{
	if (!frame_fits()) return;
	finish_pending();
	hist_begin(&doc);
	if (doc_frame_insert(&doc, doc.curFrame + 1, copy ? doc.curFrame : -1)) { hist_commit(); doc.dirty = true; }
	else { hist_abort(); set_status("Out of memory"); }
	frame_thumbs_reset();
	layerThumbsDirty = true;
	after_doc_change();
}

// Tag containing frame f (the last one added wins), or -1.
static int tag_at(int f)
{
	for (int t = doc.tagCount - 1; t >= 0; t--)
		if (f >= doc.tags[t].from && f <= doc.tags[t].to) return t;
	return -1;
}

static const char* DIR_NAMES[TAG_DIR_COUNT] = { "Forward", "Reverse", "Ping-pong" };


static void frame_act_label(int a, char* out, size_t n)
{
	int t = tag_at(doc.curFrame);
	switch (a)
	{
	case FA_NEW: snprintf(out, n, "New blank frame"); break;
	case FA_DUP: snprintf(out, n, "Duplicate frame  (Y)"); break;
	case FA_DEL: snprintf(out, n, "Delete frame"); break;
	case FA_LINK: snprintf(out, n, "Link layer's cel to previous frame"); break;
	case FA_UNLINK: snprintf(out, n, "Unlink layer's cel"); break;
	case FA_COPYNEXT: snprintf(out, n, "Copy layer's cel to the next frame"); break;
	case FA_CLEAR: snprintf(out, n, "Clear layer's cel"); break;
	case FA_MS: snprintf(out, n, "Frame duration... (%d ms)", doc_frame_ms(&doc, doc.curFrame)); break;
	case FA_FPS: snprintf(out, n, "All frames: set FPS..."); break;
	case FA_TAGNEW: snprintf(out, n, "New tag starting here"); break;
	case FA_TAGSTART: snprintf(out, n, "Tag starts here"); break;
	case FA_TAGEND: snprintf(out, n, "Tag ends here"); break;
	case FA_TAGDIR: snprintf(out, n, "Tag direction: %s", t >= 0 ? DIR_NAMES[doc.tags[t].dir % TAG_DIR_COUNT] : "-"); break;
	case FA_TAGNAME: snprintf(out, n, "Rename tag..."); break;
	case FA_TAGDEL: snprintf(out, n, "Delete tag"); break;
	case FA_PLAYTAG:
		if (playTag >= 0 && playTag < doc.tagCount) snprintf(out, n, "Play: tag \"%s\"", doc.tags[playTag].name);
		else snprintf(out, n, "Play: all frames");
		break;
	case FA_BIGVIEW: snprintf(out, n, "Large preview: %s", bigPreview ? "On" : "Off"); break;
	}
}

static void run_frame_action(int a)
{
	int f = doc.curFrame, t = tag_at(f);
	bool needTag = a == FA_TAGDIR || a == FA_TAGNAME || a == FA_TAGDEL;
	if (needTag && t < 0) { set_status("No tag on this frame"); return; }
	switch (a)
	{
	case FA_NEW: frame_add(false); break;
	case FA_DUP: frame_add(true); break;
	case FA_DEL:
		if (doc.frameCount < 2) { set_status("Can't delete the last frame"); break; }
		finish_pending();
		hist_begin(&doc);
		if (doc_frame_delete(&doc, f)) { hist_commit(); doc.dirty = true; } else hist_abort();
		frame_thumbs_reset();
		break;
	case FA_LINK:
	case FA_UNLINK:
	{
		finish_pending();
		hist_begin(&doc);
		bool ok = a == FA_LINK ? doc_cel_link_prev(&doc, doc.curLayer, f) : doc_cel_unlink(&doc, doc.curLayer, f);
		if (ok) { hist_commit(); doc.dirty = true; frame_thumbs_reset(); }
		else
		{
			hist_abort();
			set_status(a == FA_LINK ? (f == 0 ? "First frame: nothing before it" : "Already linked")
			                        : "This cel isn't linked");
		}
		break;
	}
	case FA_COPYNEXT:
	{
		// the drawing on this layer goes onto the next frame (a new one at the end), and you follow it
		bool last = f + 1 >= doc.frameCount;
		if (last && !frame_fits()) break;
		if (!last && doc_layer_locked(&doc, doc.curLayer)) { set_status("Layer is locked (Y: layers)"); break; }
		finish_pending();
		hist_begin(&doc);
		bool ok = !last || doc_frame_insert(&doc, f + 1, -1);
		ok = ok && doc_cel_copy_next(&doc, doc.curLayer, f);
		if (ok) { hist_commit(); doc.curFrame = f + 1; doc.dirty = true; set_status("Copied to the next frame"); }
		else { if (last && doc.frameCount > f + 1) doc_frame_delete(&doc, f + 1); doc.curFrame = f; hist_abort(); set_status("Out of memory"); }
		frame_thumbs_reset();
		break;
	}
	case FA_CLEAR:
		if (!layer_editable()) break;
		finish_pending();
		hist_begin_cel(&doc);
		memset(doc_pixels(&doc), 0, (size_t)doc.w * doc.h);   // a linked cel clears in every frame that shares it
		hist_commit();
		doc.dirty = true;
		frame_thumbs_reset();
		break;
	case FA_MS:
	{
		int ms = ask_number("Frame duration in ms (10-9999)", doc_frame_ms(&doc, f), 9999);
		if (ms) { hist_push_meta(&doc, 0); doc.frameMs[f] = ms < 10 ? 10 : ms; doc.dirty = true; }
		break;
	}
	case FA_FPS:
	{
		int fps = ask_number("Frames per second (1-50)", doc.fps ? doc.fps : 8, 50);
		if (fps)
		{
			hist_push_meta(&doc, 0);
			doc.fps = fps;
			for (int i = 0; i < DOC_MAX_FRAMES; i++) doc.frameMs[i] = 0;   // 0 = use the FPS
			doc.dirty = true;
		}
		break;
	}
	case FA_TAGNEW:
		if (doc.tagCount >= DOC_MAX_TAGS) { set_status("16 tags max"); break; }
		hist_push_meta(&doc, 0);
		t = doc.tagCount++;
		memset(&doc.tags[t], 0, sizeof(Tag));
		snprintf(doc.tags[t].name, TAG_NAME_MAX, "tag %d", t + 1);
		doc.tags[t].from = doc.tags[t].to = (uint8_t)f;
		doc.dirty = true;
		set_status("Tag added: go to its last frame, A > Tag ends here");
		break;
	case FA_TAGSTART:
	case FA_TAGEND:
	{
		// the nearest tag on that side of this frame
		int best = -1;
		for (int k = 0; k < doc.tagCount; k++)
		{
			const Tag* T = &doc.tags[k];
			if (a == FA_TAGEND && T->from <= f && (best < 0 || T->from > doc.tags[best].from)) best = k;
			if (a == FA_TAGSTART && T->to >= f && (best < 0 || T->to < doc.tags[best].to)) best = k;
		}
		if (best < 0) { set_status(a == FA_TAGEND ? "No tag starts before this frame" : "No tag ends after this frame"); break; }
		hist_push_meta(&doc, 0);
		if (a == FA_TAGEND) doc.tags[best].to = (uint8_t)f; else doc.tags[best].from = (uint8_t)f;
		doc.dirty = true;
		break;
	}
	case FA_TAGDIR: hist_push_meta(&doc, 0x300 + t); doc.tags[t].dir = (doc.tags[t].dir + 1) % TAG_DIR_COUNT; doc.dirty = true; break;
	case FA_TAGNAME:
	{
		char nm[DOC_NAME_MAX];
		if (ask_text("Tag name", doc.tags[t].name, nm, TAG_NAME_MAX))
		{
			hist_push_meta(&doc, 0);
			snprintf(doc.tags[t].name, TAG_NAME_MAX, "%.15s", nm);
			doc.dirty = true;
		}
		break;
	}
	case FA_TAGDEL:
		hist_push_meta(&doc, 0);
		memmove(&doc.tags[t], &doc.tags[t + 1], sizeof(Tag) * (doc.tagCount - t - 1));
		doc.tagCount--;
		if (playTag == t) playTag = -1;
		else if (playTag > t) playTag--;
		doc.dirty = true;
		break;
	case FA_PLAYTAG:
		// cycle: all frames, then each tag
		playTag = playTag + 1 >= doc.tagCount ? -1 : playTag + 1;
		break;
	case FA_BIGVIEW:
		bigPreview = !bigPreview;
		if (bigPreview) set_status("Large preview on (A > Large preview to switch back)");
		break;
	}
	layerThumbsDirty = true;
	after_doc_change();
}

static void update_frames(u32 kDown)
{
	if (kDown & (KEY_B | KEY_X)) { mode = MODE_DRAW; return; }
	u32 rep = hidKeysDownRepeat();
	if (rep & KEY_DLEFT) set_frame(doc.curFrame - 1);
	if (rep & KEY_DRIGHT) set_frame(doc.curFrame + 1);
	if (rep & (KEY_DUP | KEY_DDOWN))
	{
		int ms = doc_frame_ms(&doc, doc.curFrame) + ((rep & KEY_DUP) ? 10 : -10);
		hist_push_meta(&doc, 0x200 + doc.curFrame);   // holding the button is one undo step
		doc.frameMs[doc.curFrame] = ms < 10 ? 10 : ms > 9990 ? 9990 : ms;
		doc.dirty = true;
		playN = 0;
	}
	if (kDown & (KEY_L | KEY_R))
	{
		int dir = (kDown & KEY_R) ? 1 : -1;
		hist_begin(&doc);
		if (doc_frame_move(&doc, doc.curFrame, dir)) { hist_commit(); doc.dirty = true; frame_thumbs_reset(); after_doc_change(); }
		else hist_abort();
	}
	if (kDown & KEY_Y) run_frame_action(FA_DUP);
	if (kDown & KEY_SELECT) { playOn = !playOn; playN = 0; }
	if (kDown & KEY_A) { frameActSel = 0; mode = MODE_FRAME_ACT; }
}

// Touch bar: [<] n/N [>] [+] [onion] [play]
static void bar_touch(int x)
{
	if (x < 44) set_frame(doc.curFrame - 1);
	else if (x < 120) open_frames();
	else if (x < 164) set_frame(doc.curFrame + 1);
	else if (x < 212) frame_add(true);
	else if (x < 270) { onionOn = !onionOn; animDirty = true; set_status(onionOn ? "Onion skin on" : "Onion skin off"); }
	else { playOn = !playOn; playN = 0; }
}

static void make_frame_thumb(int f)
{
	static uint32_t comp[DOC_MAX_W * DOC_MAX_H], th[PXD_THUMB * PXD_THUMB];
	int w = doc.w, h = doc.h;
	int up = PXD_THUMB / (w > h ? w : h), tw, tht;
	if (up >= 1) { tw = w * up; tht = h * up; }
	else if (w >= h) { tw = PXD_THUMB; tht = h * PXD_THUMB / w; }
	else { tht = PXD_THUMB; tw = w * PXD_THUMB / h; }
	if (tw < 1) tw = 1;
	if (tht < 1) tht = 1;
	doc_composite(&doc, f, comp);
	for (int y = 0; y < tht; y++)
		for (int x = 0; x < tw; x++)
			th[y * tw + x] = comp[(y * h / tht) * w + x * w / tw];
	gfx_thumb_set(FRAME_THUMB_SLOT + f, th, tw, tht);
	frameThumbOk[f] = true;
}

static void thumb_size(int* tw, int* th)
{
	int w = doc.w, h = doc.h;
	int up = PXD_THUMB / (w > h ? w : h);
	if (up >= 1) { *tw = w * up; *th = h * up; }
	else if (w >= h) { *tw = PXD_THUMB; *th = h * PXD_THUMB / w; }
	else { *th = PXD_THUMB; *tw = w * PXD_THUMB / h; }
	if (*tw < 1) *tw = 1;
	if (*th < 1) *th = 1;
}

static const u32 TAG_COLORS[4] = { 0xFF28AAFF, 0xFFD25AFF, 0xFF64DC78, 0xFFF0B43C };  // ABGR for C2D

static void draw_frames_panel(void)
{
	if (bigPreview && mode == MODE_FRAMES)
	{
		// large preview: the animation as big as the top screen allows, one line of info below
		C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, C_BG_BOT);
		float bw = 400, bh = 222, s = 1;
		while (doc.w * (s + 1) <= bw && doc.h * (s + 1) <= bh) s += 1;     // whole-number scales stay crisp
		while (doc.w * s > bw || doc.h * s > bh) s /= 2;
		float px = floorf((bw - doc.w * s) / 2), py = floorf((bh - doc.h * s) / 2);
		if (anim_playing()) gfx_draw_play(&doc, px, py, s, flipPreview);
		else gfx_draw_canvas_ex(&doc, px, py, s, flipPreview);
		C2D_DrawRectSolid(0, 224, 0.5f, 400, 16, C_PANEL);
		int shown = anim_playing() ? playShown : doc.curFrame;
		if (statusTimer > 0) gfx_text(6, 225, 0.42f, C_ACCENT, "%s", status);
		else gfx_text(6, 225, 0.42f, playOn ? C_ACCENT : C_DIM, "Frame %d/%d  %d ms  %s   SELECT play  </> frame  A actions  B close",
		              shown + 1, doc.frameCount, doc_frame_ms(&doc, shown), playOn ? "playing" : "stopped");
		return;
	}
	C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, C_PANEL);
	gfx_text(8, 4, 0.52f, C_TEXT, "Frames");
	gfx_text(64, 6, 0.38f, C_DIM, "</> frame  Up/Down time  L/R move  Y add  A actions  SELECT play  B close");

	// strip of thumbnails
	const float cw = 46, cy = 44, cs = 40;
	int vis = 8, first = doc.curFrame - vis / 2;
	if (first > doc.frameCount - vis) first = doc.frameCount - vis;
	if (first < 0) first = 0;
	float x0 = 16;
	int built = 0;
	// tag bars above the cells
	for (int t = 0; t < doc.tagCount; t++)
	{
		const Tag* T = &doc.tags[t];
		int a = T->from < first ? first : T->from, b = T->to > first + vis - 1 ? first + vis - 1 : T->to;
		if (a > b) continue;
		float y = 22 + (t % 2) * 10;
		float xa = x0 + (a - first) * cw, xb = x0 + (b - first + 1) * cw - 4;
		C2D_DrawRectSolid(xa, y, 0.5f, xb - xa, 9, TAG_COLORS[t % 4]);
		gfx_text(xa + 2, y - 1, 0.32f, COL(0x10, 0x10, 0x14), "%s%s", T->name,
		         T->dir == TAG_REVERSE ? " <" : T->dir == TAG_PINGPONG ? " <>" : "");
	}
	int tw, th;
	thumb_size(&tw, &th);
	for (int f = first; f < doc.frameCount && f < first + vis; f++)
	{
		float x = x0 + (f - first) * cw;
		bool cur = f == doc.curFrame;
		C2D_DrawRectSolid(x - 2, cy - 2, 0.5f, cs + 4, cs + 4, cur ? C_ACCENT : C_SEL);
		if (!frameThumbOk[f] && built < 6) { make_frame_thumb(f); built++; }
		float sc = fminf(cs / tw, cs / th);
		if (frameThumbOk[f])
			gfx_draw_thumb_ex(FRAME_THUMB_SLOT + f, tw, th, x + (cs - tw * sc) / 2, cy + (cs - th * sc) / 2, sc, true);
		if (f > 0 && doc.layers[doc.curLayer].cels[f] == doc.layers[doc.curLayer].cels[f - 1])
			C2D_DrawRectSolid(x - 6, cy + cs / 2 - 1, 0.5f, 8, 3, C_ACCENT);   // linked to the previous frame
		gfx_text(x, cy + cs + 3, 0.4f, cur ? C_ACCENT : C_TEXT, "%d", f + 1);
		gfx_text(x + 14, cy + cs + 4, 0.34f, C_DIM, "%dms", doc_frame_ms(&doc, f));
	}
	if (first > 0) gfx_text(2, cy + 12, 0.5f, C_DIM, "<");
	if (first + vis < doc.frameCount) gfx_text(388, cy + 12, 0.5f, C_DIM, ">");

	// info + preview
	float iy = 112;
	int f = doc.curFrame;
	int ms = doc_frame_ms(&doc, f);
	gfx_text(12, iy, 0.46f, C_TEXT, "Frame %d of %d", f + 1, doc.frameCount);
	gfx_text(12, iy + 16, 0.42f, C_DIM, "Duration %d ms (%.1f fps)", ms, 1000.0f / ms);
	gfx_text(12, iy + 30, 0.42f, C_DIM, "Layer \"%s\": %s", doc.layers[doc.curLayer].name,
	         doc_cel_linked(&doc, doc.curLayer, f) ? "linked cel" : "own drawing");
	int t = tag_at(f);
	if (t >= 0)
		gfx_text(12, iy + 44, 0.42f, C_DIM, "Tag \"%s\" %d-%d, %s", doc.tags[t].name, doc.tags[t].from + 1, doc.tags[t].to + 1,
		         DIR_NAMES[doc.tags[t].dir % TAG_DIR_COUNT]);
	gfx_text(12, iy + 58, 0.42f, C_DIM, "Onion skin: %s   Memory: %.1f of %d MB", onionOn ? "on" : "off",
	         doc_pixel_bytes(&doc) / 1048576.0f, (int)(PIXEL_BUDGET >> 20));
	char pl[48];
	frame_act_label(FA_PLAYTAG, pl, sizeof(pl));
	gfx_text(12, iy + 72, 0.42f, playOn ? C_ACCENT : C_DIM, "%s%s", pl, playOn ? "  (playing)" : "  (SELECT)");

	float bx = 236, by = 104, bw = 156, bh = 124;
	C2D_DrawRectSolid(bx, by, 0.5f, bw, bh, C_BG_BOT);
	float s = 1;
	while (doc.w * s * 2 <= bw && doc.h * s * 2 <= bh && s < 4) s *= 2;
	while (doc.w * s > bw || doc.h * s > bh) s /= 2;
	float px = floorf(bx + (bw - doc.w * s) / 2), py = floorf(by + (bh - doc.h * s) / 2);
	if (anim_playing()) gfx_draw_play(&doc, px, py, s, flipPreview);
	else gfx_draw_canvas_ex(&doc, px, py, s, flipPreview);

	if (statusTimer > 0) gfx_text(12, 222, 0.45f, C_ACCENT, "%s", status);

	if (mode == MODE_FRAME_ACT)
	{
		float ax = 120, ay = 4, aw = 272, ah = 232, rh = 13.4f;
		C2D_DrawRectSolid(ax, ay, 0.5f, aw, ah, COL(0x24, 0x27, 0x30));
		gfx_rect_outline(ax, ay, aw, ah, 1, C_DIM);
		for (int a = 0; a < FA_COUNT; a++)
		{
			float y = ay + 4 + a * rh;
			if (a == frameActSel) C2D_DrawRectSolid(ax + 4, y - 1, 0.5f, aw - 8, rh, C_SEL);
			char lab[64];
			frame_act_label(a, lab, sizeof(lab));
			gfx_text(ax + 10, y, 0.42f, a == frameActSel ? C_ACCENT : C_TEXT, "%s", lab);
			if (a == FA_CLEAR || a == FA_FPS || a == FA_TAGDEL)
				C2D_DrawRectSolid(ax + 6, y + rh - 1, 0.5f, aw - 12, 1, C_PANEL);
		}
	}
}

static void draw_frame_bar(void)
{
	u32 bg = COL(0x22, 0x24, 0x2C), btn = C_SEL, ic = C_TEXT;
	C2D_DrawRectSolid(0, BAR_Y, 0.5f, BOT_W, BAR_H, bg);
	float cy = BAR_Y + BAR_H / 2.0f;
	// prev / next
	C2D_DrawRectSolid(2, BAR_Y + 2, 0.5f, 40, BAR_H - 4, btn);
	C2D_DrawTriangle(14, cy, ic, 28, cy - 6, ic, 28, cy + 6, ic, 0.5f);
	gfx_text(52, BAR_Y + 3, 0.46f, C_TEXT, "%d / %d", doc.curFrame + 1, doc.frameCount);
	C2D_DrawRectSolid(122, BAR_Y + 2, 0.5f, 40, BAR_H - 4, btn);
	C2D_DrawTriangle(150, cy, ic, 136, cy - 6, ic, 136, cy + 6, ic, 0.5f);
	// add (duplicate)
	C2D_DrawRectSolid(166, BAR_Y + 2, 0.5f, 44, BAR_H - 4, btn);
	C2D_DrawRectSolid(181, cy - 1, 0.5f, 14, 2, ic);
	C2D_DrawRectSolid(187, cy - 7, 0.5f, 2, 14, ic);
	// onion toggle
	C2D_DrawRectSolid(214, BAR_Y + 2, 0.5f, 54, BAR_H - 4, onionOn ? C_ACCENT : btn);
	gfx_text(220, BAR_Y + 3, 0.44f, onionOn ? COL(0x10, 0x10, 0x14) : C_TEXT, "Onion");
	// play / stop
	C2D_DrawRectSolid(272, BAR_Y + 2, 0.5f, 46, BAR_H - 4, playOn ? C_ACCENT : btn);
	u32 pc = playOn ? COL(0x10, 0x10, 0x14) : ic;
	if (playOn)
	{
		C2D_DrawRectSolid(287, cy - 6, 0.5f, 5, 12, pc);
		C2D_DrawRectSolid(297, cy - 6, 0.5f, 5, 12, pc);
	}
	else C2D_DrawTriangle(301, cy, pc, 289, cy - 7, pc, 289, cy + 7, pc, 0.5f);
}

// ---- home screen ---------------------------------------------------------------------

typedef struct {
	char name[DOC_NAME_MAX];
	long mtime;
	int w, h, layers, frames, tw, th;
	bool loaded, failed;
} GalItem;

#define LOGO_SLOT (GFX_THUMB_SLOTS - 1)   // last thumbnail slot holds the app icon
extern const int APP_ICON_W, APP_ICON_H;
extern const uint32_t APP_ICON[];
static bool logoReady;

static GalItem gal[GFX_THUMB_SLOTS - 1];
static int galCount, galSel, homeTab = 1, homeNewSel;
static int galSkipped;                  // projects that didn't fit in the list
static float galScroll;                 // pixels scrolled in the gallery grid
static bool homeFromCanvas;             // B goes back to the open canvas
static int homeTouch;                   // 0 none, 1 tile tap, 2 scrolling
static int homeTouchY, homeTouchStartY, homeTouchTile;
static uint32_t thumbBuf[PXD_THUMB * PXD_THUMB];

#define HOME_TAB_H 28
#define HOME_TOP 32
#define HOME_BAR_Y 208
#define TILE_W 78
#define TILE_H 88
#define NEW_TILE_H 84

static int gal_cmp(const void* a, const void* b)
{
	const GalItem *x = a, *y = b;
	bool ax = !strcmp(x->name, "_autosave"), ay = !strcmp(y->name, "_autosave");
	if (ax != ay) return ax ? -1 : 1;              // recovery file first
	if (x->mtime != y->mtime) return x->mtime > y->mtime ? -1 : 1;
	return strcasecmp(x->name, y->name);
}

static void scan_gallery(void)
{
	ensure_dirs();
	galCount = 0;
	DIR* dir = opendir(SAVE_DIR);
	if (dir)
	{
		struct dirent* e;
		galSkipped = 0;
		while ((e = readdir(dir)))
		{
			size_t n = strlen(e->d_name);
			if (n <= 4 || n - 4 >= DOC_NAME_MAX || strcasecmp(e->d_name + n - 4, ".pxd")) continue;
			if (galCount >= GFX_THUMB_SLOTS - 1) { galSkipped++; continue; }
			GalItem* g = &gal[galCount++];
			memset(g, 0, sizeof(*g));
			memcpy(g->name, e->d_name, n - 4);
			char path[200];
			project_path(path, sizeof(path), g->name);
			struct stat st;
			if (stat(path, &st) == 0) g->mtime = (long)st.st_mtime;
		}
		closedir(dir);
	}
	qsort(gal, galCount, sizeof(GalItem), gal_cmp);
	if (galSel >= galCount) galSel = galCount ? galCount - 1 : 0;
}

// Load one missing thumbnail per call (keeps the screen responsive).
static void gallery_load_some(void)
{
	int firstRow = (int)(galScroll / TILE_H), lastRow = firstRow + 3;
	for (int i = firstRow * 4; i < galCount && i < lastRow * 4; i++)
	{
		GalItem* g = &gal[i];
		if (g->loaded) continue;
		g->loaded = true;
		char path[200];
		project_path(path, sizeof(path), g->name);
		bool has = false;
		if (!pxd_read_info(path, &g->w, &g->h, &g->layers, &g->frames, &has, thumbBuf, &g->tw, &g->th)) { g->failed = true; return; }
		if (!has)
		{
			// older file without a stored thumbnail: build one from the project
			static Doc tmp;
			if (pxd_load(&tmp, path)) { g->failed = true; return; }
			pxd_make_thumb(&tmp, thumbBuf, &g->tw, &g->th);
			doc_free(&tmp);
		}
		gfx_thumb_set(i, thumbBuf, g->tw, g->th);
		return;
	}
}

static void open_home(bool fromCanvas)
{
	finish_pending();
	homeFromCanvas = fromCanvas;
	scan_gallery();
	if (galCount == 0) homeTab = 0;
	galScroll = 0;
	galSel = 0;
	mode = MODE_HOME;
}

static void gallery_scroll_to_sel(void)
{
	float rowY = (galSel / 4) * TILE_H, viewH = HOME_BAR_Y - HOME_TOP;
	if (rowY < galScroll) galScroll = rowY;
	if (rowY + TILE_H > galScroll + viewH) galScroll = rowY + TILE_H - viewH;
}

static void home_create(int i)
{
	const Preset* p = &PRESETS[i];
	if (p->w) request_new(p->w, p->h);
	else
	{
		int w = ask_number("Width (1-320)", 32, DOC_MAX_W);
		int h = w ? ask_number("Height (1-240)", 32, DOC_MAX_H) : 0;
		if (w && h) request_new(w, h);
	}
}

static void home_open(void)
{
	if (!galCount) return;
	snprintf(openFiles[0], sizeof(openFiles[0]), "%s", gal[galSel].name);
	openSel = 0;
	if (doc.dirty && homeFromCanvas) confirm(ACT_OPEN, "Discard unsaved changes?");
	else run_action(ACT_OPEN);
}

static void gallery_rename(void)
{
	char nm[DOC_NAME_MAX], from[200], to[200];
	if (!galCount || !ask_text("New name", gal[galSel].name, nm, sizeof(nm))) return;
	project_path(from, sizeof(from), gal[galSel].name);
	project_path(to, sizeof(to), nm);
	if (file_exists(to)) { set_status("A project with that name exists"); return; }
	if (rename(from, to)) { set_status("Rename failed"); return; }
	if (!strcmp(doc.name, gal[galSel].name)) snprintf(doc.name, sizeof(doc.name), "%s", nm);
	set_status("Renamed");
	scan_gallery();
}

static void gallery_duplicate(void)
{
	if (!galCount) return;
	char from[200], to[200], nm[DOC_NAME_MAX];
	project_path(from, sizeof(from), gal[galSel].name);
	for (int k = 1; k < 100; k++)
	{
		if (k == 1) snprintf(nm, sizeof(nm), "%.*s_copy", DOC_NAME_MAX - 12, gal[galSel].name);
		else snprintf(nm, sizeof(nm), "%.*s_copy%d", DOC_NAME_MAX - 12, gal[galSel].name, k);
		project_path(to, sizeof(to), nm);
		if (!file_exists(to)) break;
	}
	FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
	bool ok = in && out;
	char buf[4096];
	size_t n;
	while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0)
		ok = fwrite(buf, 1, n, out) == n;
	if (in) fclose(in);
	if (out && fclose(out)) ok = false;
	if (!ok) { remove(to); set_status("Duplicate failed"); return; }
	set_status("Duplicated");
	scan_gallery();
}

static void gallery_delete(void)
{
	char path[200];
	project_path(path, sizeof(path), gal[galSel].name);
	if (remove(path)) set_status("Delete failed");
	else
	{
		if (!strcmp(doc.name, gal[galSel].name)) { doc.name[0] = 0; doc.dirty = true; }
		set_status("Deleted");
	}
	scan_gallery();
	for (int i = 0; i < galCount; i++) gal[i].loaded = false;   // slots moved
	mode = MODE_HOME;
}

static void gallery_export(void)
{
	if (!galCount) return;
	char path[200];
	project_path(path, sizeof(path), gal[galSel].name);
	static Doc tmp;
	if (pxd_load(&tmp, path)) { set_status("Couldn't open project"); return; }
	run_export_doc(&tmp, gal[galSel].name);
	doc_free(&tmp);
}

static const char* GAL_ACTS[] = { "Open", "Rename", "Copy", "Delete", "Export" };

static void gallery_action(int a)
{
	if (!galCount) return;
	switch (a)
	{
	case 0: home_open(); break;
	case 1: gallery_rename(); break;
	case 2: gallery_duplicate(); for (int i = 0; i < galCount; i++) gal[i].loaded = false; break;
	case 3:
	{
		char msg[96];
		snprintf(msg, sizeof(msg), "Delete %.40s?", gal[galSel].name);
		confirm(ACT_DELETE_GAL, msg);
		break;
	}
	case 4: gallery_export(); break;
	}
}

static void update_home(u32 kDown, u32 kHeld, touchPosition t)
{
	if (kDown & KEY_START)
	{
		if (doc.dirty) confirm(ACT_EXIT, "Exit without saving?");
		else { remove(AUTOSAVE_PATH); running = false; }
		return;
	}
	if (kDown & (KEY_L | KEY_R)) homeTab ^= 1;
	if ((kDown & KEY_B) && homeFromCanvas) { mode = MODE_DRAW; return; }
	if ((kDown & KEY_B) && !homeFromCanvas) { mode = MODE_DRAW; set_status("Blank 32x32 canvas - START > Home to come back"); return; }

	int count = homeTab ? galCount : PRESET_COUNT;
	int* sel = homeTab ? &galSel : &homeNewSel;
	if (count)
	{
		if (kDown & KEY_DLEFT) *sel = (*sel + count - 1) % count;
		if (kDown & KEY_DRIGHT) *sel = (*sel + 1) % count;
		if ((kDown & KEY_DUP) && *sel >= 4) *sel -= 4;
		if ((kDown & KEY_DDOWN) && *sel + 4 < count) *sel += 4;
		if (homeTab) gallery_scroll_to_sel();
	}
	if (kDown & KEY_A)
	{
		if (homeTab) home_open();
		else home_create(homeNewSel);
		return;
	}
	if (homeTab && (kDown & KEY_X)) gallery_action(3);   // X: delete
	if (homeTab && (kDown & KEY_Y)) gallery_action(1);   // Y: rename

	if (kDown & KEY_TOUCH)
	{
		homeTouch = 0;
		if (t.py < HOME_TAB_H) { homeTab = t.px >= BOT_W / 2; return; }
		if (homeTab && t.py >= HOME_BAR_Y)
		{
			int b = t.px / (BOT_W / 5);
			if (b >= 0 && b < 5) gallery_action(b);
			return;
		}
		homeTouch = 1;
		homeTouchY = homeTouchStartY = t.py;
		int col = (t.px - 2) / TILE_W, row;
		if (homeTab) row = (int)((t.py - HOME_TOP + galScroll) / TILE_H);
		else row = (t.py - HOME_TOP) / (NEW_TILE_H + 4);
		homeTouchTile = (col >= 0 && col < 4 && row >= 0) ? row * 4 + col : -1;
	}
	else if ((kHeld & KEY_TOUCH) && homeTouch)
	{
		if (homeTab && (homeTouch == 2 || abs(t.py - homeTouchStartY) > 6))
		{
			homeTouch = 2;    // drag scrolls the gallery
			galScroll -= t.py - homeTouchY;
			float maxScroll = ((galCount + 3) / 4) * TILE_H - (HOME_BAR_Y - HOME_TOP);
			if (galScroll > maxScroll) galScroll = maxScroll;
			if (galScroll < 0) galScroll = 0;
		}
		homeTouchY = t.py;
	}
	else if (!(kHeld & KEY_TOUCH) && homeTouch)
	{
		if (homeTouch == 1 && homeTouchTile >= 0 && homeTouchTile < count)
		{
			if (homeTouchTile == *sel)
			{
				if (homeTab) home_open();       // tap the selected tile again to open it
				else home_create(homeTouchTile);
			}
			else *sel = homeTouchTile;
		}
		homeTouch = 0;
	}
}

static void draw_home_bottom(void)
{
	C2D_DrawRectSolid(0, 0, 0.5f, BOT_W, BOT_H, C_BG_TOP);
	// tabs
	for (int i = 0; i < 2; i++)
	{
		float x = i * BOT_W / 2;
		C2D_DrawRectSolid(x + 2, 2, 0.5f, BOT_W / 2 - 4, HOME_TAB_H - 4, homeTab == i ? C_SEL : C_PANEL);
		const char* lab = i ? "Gallery" : "New";
		gfx_text(x + (BOT_W / 2 - gfx_text_width(0.55f, lab)) / 2, 6, 0.55f, homeTab == i ? C_ACCENT : C_TEXT, "%s", lab);
	}
	if (!homeTab)
	{
		for (int i = 0; i < PRESET_COUNT; i++)
		{
			float x = 2 + (i % 4) * TILE_W, y = HOME_TOP + (i / 4) * (NEW_TILE_H + 4);
			C2D_DrawRectSolid(x + 2, y, 0.5f, TILE_W - 4, NEW_TILE_H, i == homeNewSel ? C_SEL : C_PANEL);
			if (i == homeNewSel) gfx_rect_outline(x + 2, y, TILE_W - 4, NEW_TILE_H, 2, C_ACCENT);
			const Preset* p = &PRESETS[i];
			if (p->w)
			{
				// canvas shape preview
				float sc = fminf(56.0f / p->w, 50.0f / p->h);
				float pw = p->w * sc, ph = p->h * sc;
				gfx_draw_checker(x + (TILE_W - pw) / 2, y + 6 + (50 - ph) / 2, pw, ph, 4);
			}
			else
				gfx_text(x + 30, y + 18, 1.0f, C_DIM, "?");
			gfx_text(x + (TILE_W - gfx_text_width(0.45f, p->label)) / 2, y + 64, 0.45f, C_TEXT, "%s", p->label);
		}
		gfx_text(8, 222, 0.42f, C_DIM, "Tap a size twice (or A) to start.  L/R: switch tab");
		return;
	}
	if (galCount == 0)
	{
		gfx_text(16, 80, 0.5f, C_DIM, "No saved projects yet.");
		gfx_text(16, 100, 0.45f, C_DIM, "Save one with START > Save, and it shows up here.");
		return;
	}
	for (int i = 0; i < galCount; i++)
	{
		float x = 2 + (i % 4) * TILE_W, y = HOME_TOP + (i / 4) * TILE_H - galScroll;
		if (y + TILE_H < HOME_TOP || y > HOME_BAR_Y) continue;
		GalItem* g = &gal[i];
		C2D_DrawRectSolid(x + 2, y, 0.5f, TILE_W - 4, TILE_H - 4, i == galSel ? C_SEL : C_PANEL);
		if (g->loaded && !g->failed)
			gfx_draw_thumb(i, g->tw, g->th, x + (TILE_W - g->tw) / 2, y + 3 + (64 - g->th) / 2, 1);
		else
			gfx_text(x + 20, y + 26, 0.45f, C_DIM, g->failed ? "error" : "...");
		if (g->loaded && !g->failed && g->frames > 1)
		{
			// animation badge: frame count in the top-right corner
			char fb[16];
			snprintf(fb, sizeof(fb), "%df", g->frames);
			float bw2 = gfx_text_width(0.36f, fb) + 6;
			C2D_DrawRectSolid(x + TILE_W - 4 - bw2, y + 2, 0.5f, bw2, 11, C_ACCENT);
			gfx_text(x + TILE_W - 1 - bw2, y + 2, 0.36f, COL(0x10, 0x10, 0x14), "%s", fb);
		}
		if (i == galSel) gfx_rect_outline(x + 2, y, TILE_W - 4, TILE_H - 4, 2, C_ACCENT);
		bool rec = !strcmp(g->name, "_autosave");
		char nm[14];
		snprintf(nm, sizeof(nm), "%.12s", rec ? "recovered" : g->name);
		gfx_text(x + (TILE_W - gfx_text_width(0.4f, nm)) / 2, y + 70, 0.4f, rec ? C_ACCENT : C_TEXT, "%s", nm);
	}
	// keep the tabs and action bar on top of scrolled tiles
	C2D_DrawRectSolid(0, 0, 0.5f, BOT_W, HOME_TOP - 2, C_BG_TOP);
	for (int i = 0; i < 2; i++)
	{
		float x = i * BOT_W / 2;
		C2D_DrawRectSolid(x + 2, 2, 0.5f, BOT_W / 2 - 4, HOME_TAB_H - 4, homeTab == i ? C_SEL : C_PANEL);
		const char* lab = i ? "Gallery" : "New";
		gfx_text(x + (BOT_W / 2 - gfx_text_width(0.55f, lab)) / 2, 6, 0.55f, homeTab == i ? C_ACCENT : C_TEXT, "%s", lab);
	}
	C2D_DrawRectSolid(0, HOME_BAR_Y - 2, 0.5f, BOT_W, BOT_H - HOME_BAR_Y + 2, C_BG_TOP);
	for (int b = 0; b < 5; b++)
	{
		float x = b * (BOT_W / 5);
		u32 bg = b == 3 ? C2D_Color32(120, 50, 50, 255) : b == 0 ? C2D_Color32(40, 120, 70, 255) : C_PANEL;
		C2D_DrawRectSolid(x + 2, HOME_BAR_Y + 2, 0.5f, BOT_W / 5 - 4, BOT_H - HOME_BAR_Y - 6, bg);
		gfx_text(x + (BOT_W / 5 - gfx_text_width(0.45f, GAL_ACTS[b])) / 2, HOME_BAR_Y + 9, 0.45f, C_TEXT, "%s", GAL_ACTS[b]);
	}
}

static void draw_home_top(void)
{
	C2D_DrawRectSolid(0, 0, 0.5f, 400, 240, C_BG_TOP);
	if (!logoReady)
	{
		gfx_thumb_set(LOGO_SLOT, APP_ICON, APP_ICON_W, APP_ICON_H);
		logoReady = true;
	}
	gfx_draw_thumb_ex(LOGO_SLOT, APP_ICON_W, APP_ICON_H, 10, 4, 1, false);
	gfx_text(64, 10, 0.8f, C_ACCENT, APP_NAME);
	gfx_text(66 + gfx_text_width(0.8f, APP_NAME), 20, 0.42f, C_DIM, "v" APP_VERSION);
	gfx_text(66, 34, 0.42f, C_DIM, "Pixel art, sprites & animation");
	gfx_text(400 - 8 - gfx_text_width(0.42f, "START: exit"), 14, 0.42f, C_DIM, "START: exit");
	float bx = 120, by = 60, bw = 260, bh = 150;
	C2D_DrawRectSolid(bx, by, 0.5f, bw, bh, C_BG_BOT);
	if (!homeTab)
	{
		const Preset* p = &PRESETS[homeNewSel];
		gfx_text(16, 66, 0.6f, C_TEXT, "New canvas");
		gfx_text(16, 88, 0.5f, C_DIM, "%s", p->w ? p->label : "Custom size");
		if (p->w)
		{
			float sc = fminf((bw - 20) / p->w, (bh - 20) / p->h);
			if (sc >= 1) sc = floorf(sc);
			gfx_draw_checker(bx + (bw - p->w * sc) / 2, by + (bh - p->h * sc) / 2, p->w * sc, p->h * sc, 6);
		}
		gfx_text(16, 222, 0.42f, C_DIM, "D-pad: choose   A: create   L/R: Gallery%s", homeFromCanvas ? "   B: back" : "");
		return;
	}
	if (!galCount)
	{
		gfx_text(16, 66, 0.55f, C_DIM, "Gallery is empty");
		return;
	}
	GalItem* g = &gal[galSel];
	bool rec = !strcmp(g->name, "_autosave");
	gfx_text(16, 66, 0.55f, rec ? C_ACCENT : C_TEXT, "%.18s", rec ? "Recovered work" : g->name);
	if (g->loaded && !g->failed)
	{
		gfx_text(16, 88, 0.45f, C_DIM, "%d x %d", g->w, g->h);
		gfx_text(16, 104, 0.45f, C_DIM, "%d layer%s", g->layers, g->layers == 1 ? "" : "s");
		if (g->frames > 1) gfx_text(16, 120, 0.45f, C_ACCENT, "Animation: %d frames", g->frames);
		float sc = fminf((bw - 20) / g->tw, (bh - 20) / g->th);
		if (sc >= 1) sc = floorf(sc);
		gfx_draw_thumb(galSel, g->tw, g->th, bx + (bw - g->tw * sc) / 2, by + (bh - g->th * sc) / 2, sc);
	}
	gfx_text(16, 140, 0.42f, C_DIM, "%d of %d", galSel + 1, galCount);
	if (galSkipped)
		gfx_text(16, 156, 0.4f, C_ACCENT, "%d more not shown\n(the list holds %d)", galSkipped, galCount);
	gfx_text(16, 222, 0.42f, C_DIM, "A: open  X: delete  Y: rename  L/R: New tab%s", homeFromCanvas ? "  B: back" : "");
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
	if ((kDown & KEY_START) && touchTarget == TOUCH_NONE) { finish_pending(); mode = MODE_MENU; menuSel = 0; return; }
	if ((kDown & KEY_A) && touchTarget == TOUCH_NONE)
	{
		if (tool == TOOL_STAMP) refresh_stamps();
		optTool = tool;
		mode = MODE_OPTIONS;
		optSel = 0;
		return;
	}
	if (kDown & KEY_SELECT)
	{
		previewMode = !previewMode;
		playOn = previewMode && doc.frameCount > 1;   // the preview plays the animation
		playN = 0;
	}
	if (touchTarget == TOUCH_NONE) // no tool changes mid-stroke
	{
		if (kDown & KEY_X) { curve_finish(); open_frames(); return; }
		if (kDown & KEY_Y) { curve_finish(); open_layers(); return; }
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
		if ((kDown & (KEY_L | KEY_B)) && curveStage)
		{
			curve_cancel();
			kDown &= ~(KEY_L | KEY_B);
		}
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
		if ((kDown & KEY_B) && tool == TOOL_TEXT && textOn)
			text_place();
		else if ((kDown & KEY_B) && tool == TOOL_STAMP && sel_floating())
		{
			finish_floating();       // place the stamp
			sel_clear(&doc);
			after_doc_change();
		}
		else if ((kDown & KEY_B) && tool == TOOL_SELECT && sel_floating())
			finish_floating();       // apply the move/transform, keep the selection
		else if (kDown & KEY_B)
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
	if (inputMap.lefty && isNew3DS) hidCstickRead(&cp);   // the right thumb pans when the left hand draws
	else hidCircleRead(&cp);
	if (abs(cp.dx) > 16 || abs(cp.dy) > 16)
	{
		const float speed = 5.0f / 156.0f;
		viewX += cp.dx * speed / zoom();
		viewY -= cp.dy * speed / zoom();
		view_clamp();
	}

	if ((kDown & KEY_TOUCH) && touchTarget == TOUCH_NONE && bar_shown() && touch.py >= BAR_Y)
	{
		curve_finish();              // a line waiting to be bent belongs to the frame it was drawn on
		bar_touch(touch.px);
		touchTarget = TOUCH_STRIP;   // the rest of this touch does nothing
		return;
	}

	bool dropper = (kHeld & KEY_R) || tool == TOOL_EYEDROPPER;
	uint8_t value = tool == TOOL_ERASER ? PIX_TRANSPARENT : strokeValue;

	// ---- ZR held when the stylus touches down: drag the canvas (hand tool) ----
	if ((kDown & KEY_TOUCH) && (kHeld & KEY_ZR) && touch.py >= STRIP_H && touchTarget == TOUCH_NONE &&
	    !((tool == TOOL_SELECT || tool == TOOL_STAMP || tool == TOOL_TEXT) && sel_floating() && tf_hit(touch.px, touch.py) >= 0))
	{
		touchTarget = TOUCH_PAN;
		panLastX = touch.px;
		panLastY = touch.py;
		return;
	}
	if (touchTarget == TOUCH_PAN)
	{
		if (kHeld & KEY_TOUCH)
		{
			viewX -= (touch.px - panLastX) / zoom();
			viewY -= (touch.py - panLastY) / zoom();
			view_clamp();
			panLastX = touch.px;
			panLastY = touch.py;
		}
		else
			touchTarget = TOUCH_NONE;
		return;
	}

	// ---- Select / Stamp / Text: selections and floating pixels with transform handles ----
	if ((tool == TOOL_SELECT || tool == TOOL_STAMP || tool == TOOL_TEXT) && !(kHeld & KEY_R) &&
	    (touchTarget == TOUCH_CANVAS || ((kDown & KEY_TOUCH) && touch.py >= STRIP_H)))
	{
		float fx = (touch.px - canvas_sx()) / zoom(), fy = (touch.py - canvas_sy()) / zoom();  // canvas coords
		int cx = (int)floorf(fx), cy = (int)floorf(fy);
		bool crisp = tool == TOOL_TEXT ? textCrisp : tfCrisp;
		if (kDown & KEY_TOUCH)
		{
			touchTarget = TOUCH_CANVAS;
			startCX = cx;
			startCY = cy;
			selDrag = SEL_IDLE;
			tfDrag = TF_NONE;
			tfMoved = false;
			int hit = sel_floating() ? tf_hit(touch.px, touch.py) : -1;
			if (hit >= 0)
				tf_begin(hit);                                     // grabbed a handle
			else if (sel_floating() && tf_inside(fx, fy))
			{
				float x, y, w, h, a;
				sel_transform_get(&x, &y, &w, &h, &a);
				Rect r = sel_rect();
				tfMoveOffX = fx - r.x;
				tfMoveOffY = fy - r.y;
				tfDrag = TF_MOVE;
			}
			else
			{
				// outside floating pixels: drop them, then carry on with this tap
				bool wasFloating = sel_floating();
				if (tool == TOOL_TEXT && textOn) text_place();
				else if (wasFloating) { finish_floating(); if (tool == TOOL_STAMP) sel_clear(&doc); }
				if (tool == TOOL_TEXT)
				{
					if (!wasFloating && layer_editable())
					{
						text_start(cx, cy);
						touchTarget = TOUCH_STRIP;                 // the keyboard ate this touch
						after_doc_change();
						return;
					}
				}
				else if (tool == TOOL_STAMP)
				{
					uint8_t* px;
					int w, h;
					if (!stampCount) set_status("No stamps yet - save one with Select, A");
					else if (!layer_editable()) {}
					else if (stamp_load(STAMP_DIR, stampNames[stampSel], &doc, &px, &w, &h)) set_status("Couldn't load stamp");
					else
					{
						sel_float_pixels(&doc, px, w, h, cx - w / 2, cy - h / 2);   // centred on the stylus
						sel_set_float_mirror(mirrorOn ? mirrorMode : MIRROR_OFF);
	sel_set_mirror_axis(mirrorAX2, mirrorAY2);
						free(px);
						tfMoveOffX = w / 2;
						tfMoveOffY = h / 2;
						tfDrag = TF_MOVE;                          // keep dragging to position it
					}
				}
				else if (sel_contains(cx, cy) && selOp == SELOP_NEW)
					selDrag = SEL_PENDING;                         // tap = new selection, drag = move it
				else if (selMode == SELMODE_WAND)
					sel_wand(&doc, cx, cy, wandAll, wandTol, wandSampleAll, selOp);
				else if (selMode == SELMODE_LASSO)
				{
					if (selOp == SELOP_NEW) sel_clear(&doc);
					lassoPts[0] = cx;
					lassoPts[1] = cy;
					lassoCount = 1;
					selDrag = SEL_LASSO;
				}
				else
				{
					if (selOp == SELOP_NEW) sel_set(&doc, cx, cy, cx, cy);
					selDrag = SEL_DRAWING;
				}
			}
		}
		else if (kHeld & KEY_TOUCH)
		{
			if (cx != startCX || cy != startCY) tfMoved = true;
			if (tfDrag == TF_MOVE && tfMoved)
				sel_move_to(&doc, (int)floorf(fx - tfMoveOffX + 0.5f), (int)floorf(fy - tfMoveOffY + 0.5f));
			else if (tfDrag != TF_NONE && tfDrag != TF_MOVE)
				tf_drag(fx, fy, kHeld & KEY_ZR, crisp);
			else if (selDrag == SEL_PENDING && tfMoved)
			{
				// dragging inside the selection: lift it and move
				if (layer_editable())
				{
					sel_lift(&doc);
					Rect r = sel_rect();
					tfMoveOffX = startCX - r.x;
					tfMoveOffY = startCY - r.y;
					tfDrag = TF_MOVE;
					sel_move_to(&doc, (int)floorf(fx - tfMoveOffX + 0.5f), (int)floorf(fy - tfMoveOffY + 0.5f));
				}
				selDrag = SEL_IDLE;
			}
			else if (selDrag == SEL_DRAWING && selOp == SELOP_NEW)
				sel_set(&doc, startCX, startCY, cx, cy);
			else if (selDrag == SEL_LASSO && lassoCount < LASSO_MAX &&
			         (cx != lassoPts[(lassoCount - 1) * 2] || cy != lassoPts[(lassoCount - 1) * 2 + 1]))
			{
				lassoPts[lassoCount * 2] = cx;
				lassoPts[lassoCount * 2 + 1] = cy;
				lassoCount++;
			}
			selLastCX = cx;
			selLastCY = cy;
		}
		if (!(kHeld & KEY_TOUCH))
		{
			bool tap = !tfMoved;
			if (tfDrag == TF_ROTATE || tfDrag == TF_CORNER || tfDrag == TF_EDGE)
				tf_end();                                          // final, smoother resample
			else if (tfDrag == TF_MOVE && tap && tool == TOOL_TEXT)
				text_edit();                                       // tapped the text: retype it
			else if (selDrag == SEL_PENDING)
			{
				// tapped inside the selection without dragging
				if (selMode == SELMODE_WAND) sel_wand(&doc, startCX, startCY, wandAll, wandTol, wandSampleAll, SELOP_NEW);
				else sel_clear(&doc);
			}
			else if (selDrag == SEL_DRAWING)
			{
				if (selOp == SELOP_NEW) { if (tap) sel_clear(&doc); }
				else if (!tap) sel_rect_op(&doc, startCX, startCY, selLastCX, selLastCY, selOp);
			}
			else if (selDrag == SEL_LASSO)
			{
				if (lassoCount > 2) sel_lasso(&doc, lassoPts, lassoCount, selOp);
				else if (selOp == SELOP_NEW) sel_clear(&doc);
			}
			lassoCount = 0;
			selDrag = SEL_IDLE;
			tfDrag = TF_NONE;
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
					text_refresh();
				}
			}
			else if (slot >= 0 && slot < DOC_PALETTE_SIZE)
			{
				if (kHeld & KEY_R)
				{
					// R + tap a swatch: save the current color into it
					hist_push_meta(&doc, 0);
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
				text_refresh();
			}
		}
		else
		{
			touchTarget = TOUCH_CANVAS;
			int cx, cy;
			screen_to_canvas(touch.px, touch.py, &cx, &cy);
			if (curveStage && !dropper)
				curveBend = true;                    // second step of a curve: bend it
			else if (!curveStage)                    // (an eyedrop mid-curve must not move its start)
			{
			startCX = cx;
			startCY = cy;
			}
			if (curveBend) {}
			else if (dropper)
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
				shapeEndX = startCX;
				shapeEndY = startCY;
				hist_begin_cel(&doc);             // strokes only touch this layer's current frame
				tools_set_mirror_axis(mirrorAX2, mirrorAY2);
				tools_set_wrap(wrapDraw);
				strokeLastCX = cx;
				strokeLastCY = cy;
				tools_begin(&doc, mirrorOn ? mirrorMode : MIRROR_OFF);
				tools_set_clip(sel_active() ? sel_contains : NULL);   // paint inside the selection only
				strokeActive = true;
				if (tool == TOOL_FILL && fillMode != FILL_GRADIENT)
					tools_fill(cx, cy, strokeValue, fillMode == FILL_REPLACE);
				else if (tool == TOOL_FILL)
					tools_gradient_fill(cx, cy, cx, cy, strokeValue);
				else if (tool == TOOL_PENCIL || (tool == TOOL_ERASER && !eraserLine) || tool == TOOL_SHADE)
					tools_stroke_start(cx, cy, value, &brushes[tool]);
				// shapes are drawn below while dragging
			}
		}
	}

	if ((kHeld & KEY_TOUCH) && touchTarget == TOUCH_CANVAS)
	{
		int cx, cy;
		bool partial = false;                    // only a small part of the canvas texture needed redoing
		screen_to_canvas(touch.px, touch.py, &cx, &cy);
		if (curveBend)
		{
			tools_restore();
			tools_curve(startCX, startCY, cx, cy, curveEndX, curveEndY, strokeValue, &brushes[TOOL_LINE]);
			after_doc_change();
		}
		else if (dropper && !strokeActive)
			eyedrop(cx, cy);
		else if (strokeActive && (tool == TOOL_PENCIL || (tool == TOOL_ERASER && !eraserLine) || tool == TOOL_SHADE) && !(kDown & KEY_TOUCH))
		{
			tools_stroke_to(cx, cy, value, &brushes[tool]);
			if (!mirrorOn && !wrapDraw)
			{
				// a freehand stroke only changes pixels near the stylus: redraw just that box
				// instead of compositing every layer of the whole canvas (matters on an Old 3DS)
				int pad = BRUSH_MAX + 2;
				gfx_canvas_rebuild_rect(&doc, (cx < strokeLastCX ? cx : strokeLastCX) - pad, (cy < strokeLastCY ? cy : strokeLastCY) - pad,
				                        (cx > strokeLastCX ? cx : strokeLastCX) + pad, (cy > strokeLastCY ? cy : strokeLastCY) + pad);
				partial = true;
			}
			strokeLastCX = cx;
			strokeLastCY = cy;
		}
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
				if (tool == TOOL_LINE || tool == TOOL_ERASER)
				{
					if (snap) tools_snap_line(startCX, startCY, &cx, &cy);
					tools_line(startCX, startCY, cx, cy, value, &brushes[tool]);
					shapeEndX = cx;
					shapeEndY = cy;
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
		if ((strokeActive && !partial) || (kDown & KEY_B))
			after_doc_change();
	}
	else if ((kDown & KEY_TOUCH) && strokeActive)
		after_doc_change();

	if ((kUp & KEY_TOUCH) || !(kHeld & KEY_TOUCH))
	{
		if (touchTarget == TOUCH_CANVAS && curveBend)
		{
			curveStage = 1;                          // curve_finish commits it
			curve_finish();
			touchTarget = TOUCH_NONE;
			return;
		}
		if (touchTarget == TOUCH_CANVAS && strokeActive && tool == TOOL_LINE && lineCurve &&
		    (shapeEndX != startCX || shapeEndY != startCY))
		{
			// line placed: keep it pending and wait for the bend
			curveStage = 2;
			curveEndX = shapeEndX;
			curveEndY = shapeEndY;
			strokeActive = false;
			touchTarget = TOUCH_NONE;
			set_status("Now drag anywhere to bend it. B cancels");
			return;
		}
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
	if (mode == MODE_HOME || (mode == MODE_CONFIRM && confirmBack == MODE_HOME))
	{
		draw_home_bottom();
		return;
	}
	if (mode == MODE_PICKER)
	{
		picker_draw();
		return;
	}

	float z = zoom(), sx = canvas_sx(), sy = canvas_sy();
	gfx_draw_canvas(&doc, sx, sy, z, VIEW_Y, VIEW_Y + VIEW_H);

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
	bool mirrorPanel = mode == MODE_OPTIONS && optTool == TOOL_MIRROR;
	if ((mirrorOn && tool != TOOL_SELECT && tool != TOOL_EYEDROPPER) || mirrorPanel)
	{
		float axX = (mirrorAX2 ? mirrorAX2 : doc.w) / 2.0f, axY = (mirrorAY2 ? mirrorAY2 : doc.h) / 2.0f;
		u32 mc = C2D_Color32(0xFF, 0x50, 0xC8, 0xB0);
		if (mirrorMode == MIRROR_X || mirrorMode == MIRROR_XY)
			C2D_DrawRectSolid(sx + axX * z - (z >= 4 ? 1 : 0.5f), sy < VIEW_Y ? VIEW_Y : sy, 0.5f, z >= 4 ? 2 : 1, doc.h * z, mc);
		if (mirrorMode == MIRROR_Y || mirrorMode == MIRROR_XY)
		{
			float gy = sy + axY * z;
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
	bool transforming = sel_floating() && (tool == TOOL_SELECT || tool == TOOL_STAMP || tool == TOOL_TEXT);
	if (transforming) {}
	else if (sel_active() && sel_has_mask())
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

	// curve waiting to be bent: show its ends
	if (curveStage == 2)
	{
		float pts[2][2] = { { (float)startCX, (float)startCY }, { (float)curveEndX, (float)curveEndY } };
		for (int k = 0; k < 2; k++)
		{
			float px = sx + (pts[k][0] + 0.5f) * z, py = sy + (pts[k][1] + 0.5f) * z;
			C2D_DrawCircleSolid(px, py, 0.5f, 5, C2D_Color32(0, 0, 0, 255));
			C2D_DrawCircleSolid(px, py, 0.5f, 4, C2D_Color32(0x30, 0xD0, 0xFF, 255));
		}
	}

	// transform box for floating pixels
	if ((tool == TOOL_SELECT || tool == TOOL_STAMP || tool == TOOL_TEXT) && sel_floating())
	{
		float hx[9], hy[9];
		for (int k = 0; k < 9; k++) tf_screen(k, &hx[k], &hy[k]);
		u32 lc = C2D_Color32(255, 255, 255, 230), dk = C2D_Color32(0, 0, 0, 255);
		for (int k = 0; k < 4; k++)
		{
			int n = (k + 1) % 4;
			C2D_DrawLine(hx[k], hy[k], dk, hx[n], hy[n], dk, 3, 0.5f);
			C2D_DrawLine(hx[k], hy[k], lc, hx[n], hy[n], lc, 1, 0.5f);
		}
		C2D_DrawLine(hx[4], hy[4], lc, hx[8], hy[8], lc, 1, 0.5f);
		for (int k = 0; k < 8; k++)
		{
			float sz = k < 4 ? 8 : 6;
			C2D_DrawRectSolid(hx[k] - sz / 2 - 1, hy[k] - sz / 2 - 1, 0.5f, sz + 2, sz + 2, dk);
			C2D_DrawRectSolid(hx[k] - sz / 2, hy[k] - sz / 2, 0.5f, sz, sz, k < 4 ? C_ACCENT : C2D_Color32(255, 255, 255, 255));
		}
		C2D_DrawCircleSolid(hx[8], hy[8], 0.5f, 6, dk);
		C2D_DrawCircleSolid(hx[8], hy[8], 0.5f, 5, C2D_Color32(0x30, 0xD0, 0xFF, 255));
	}
	// rectangle being added / subtracted
	if (selDrag == SEL_DRAWING && selOp != SELOP_NEW)
	{
		int x0 = startCX < selLastCX ? startCX : selLastCX, x1 = startCX < selLastCX ? selLastCX : startCX;
		int y0 = startCY < selLastCY ? startCY : selLastCY, y1 = startCY < selLastCY ? selLastCY : startCY;
		gfx_rect_outline(sx + x0 * z, sy + y0 * z, (x1 - x0 + 1) * z, (y1 - y0 + 1) * z, 1,
		                 selOp == SELOP_ADD ? C2D_Color32(120, 255, 120, 255) : C2D_Color32(255, 110, 110, 255));
	}

	if (bar_shown()) draw_frame_bar();
	if (mode != MODE_DRAW && mode != MODE_SHIFT && mode != MODE_RAMP && mode != MODE_ADJUST && mode != MODE_LAYERS &&
	    mode != MODE_FRAMES && !mirrorPanel)
		C2D_DrawRectSolid(0, 0, 0.5f, BOT_W, BOT_H, C2D_Color32(0, 0, 0, 0x90));
}

// Draws the canvas into a box at the largest scale <= 1:1 that fits, centered.
// The document for previews: the playing animation, or the current frame.
static void draw_doc(float x, float y, float s)
{
	if (anim_playing()) gfx_draw_play(&doc, x, y, s, flipPreview);
	else gfx_draw_canvas_ex(&doc, x, y, s, flipPreview);
}

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
			draw_doc(px + i * doc.w * s, py + j * doc.h * s, s);
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
	gfx_text(10, iy + 22, 0.4f, C_DIM, "Frame %d/%d  %d ms  (X)", doc.curFrame + 1, doc.frameCount,
	         doc_frame_ms(&doc, doc.curFrame));

	// true-size preview
	C2D_DrawRectSolid(196, 28, 0.5f, 198, 180, C_BG_BOT);
	draw_preview_box(197, 29, 196, 178);

	// footer
	if (statusTimer > 0)
		gfx_text(10, 220, 0.5f, C_ACCENT, "%s", status);
	else if (mode == MODE_PICKER)
		gfx_text(10, 220, 0.45f, C_DIM, "Pick a color below.  A: OK   B: cancel");
	else if (tool == TOOL_SELECT)
		gfx_text(10, 222, 0.42f, C_DIM, sel_floating() ? "Handles: size/rotate (ZR: stretch)  B: apply"
		                                               : "Drag: select / move.  A: actions  B: deselect");
	else if (tool == TOOL_TEXT)
		gfx_text(10, 222, 0.42f, C_DIM, textOn ? "Drag: move  Handles: size/rotate  Tap: edit  B: place"
		                                       : "Tap the canvas to type.  A: font, size, outline");
	else if (tool == TOOL_STAMP)
		gfx_text(10, 222, 0.42f, C_DIM, sel_floating() ? "Handles: size/rotate (ZR: stretch)  B / tap outside: place"
		                                               : "Touch to place a stamp.  A: pick stamp");
	else if (curveStage == 2)
		gfx_text(10, 222, 0.42f, C_DIM, "Drag anywhere to bend the curve.  B / L: cancel");
	else if (is_shape(tool))
		gfx_text(10, 222, 0.42f, C_DIM, "Drag to draw. Hold ZR to snap, B cancels.");
	else
		gfx_text(10, 222, 0.42f, C_DIM, "L undo ZL redo R eyedrop B eraser X frames <> zoom");
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
		draw_doc(x, y, s);
		gfx_text(x, y + doc.h * s + 2, 0.4f, C_DIM, "%dx", s);
		x += doc.w * s + gap;
	}
	if (anim_playing())
		gfx_text(8, 224, 0.42f, C_DIM, "Playing frame %d/%d%s   SELECT: back to tools", playShown + 1, doc.frameCount,
		         flipPreview ? " (flipped)" : "");
	else
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
	if (mode == MODE_HOME || (mode == MODE_CONFIRM && confirmBack == MODE_HOME))
	{
		draw_home_top();
		if (mode != MODE_CONFIRM) { if (statusTimer > 0) gfx_text(16, 204, 0.45f, C_ACCENT, "%s", status); return; }
	}
	else if (mode == MODE_FRAMES || mode == MODE_FRAME_ACT)
	{
		draw_frames_panel();
		return;
	}
	else if (previewMode && mode == MODE_DRAW)
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
		draw_list("Open project", labels, openCount, openSel, "No projects in /3ds/pixel-crow/projects");
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
		if (exportTag >= doc.tagCount || (exportTag == -2 && !doc.tagCount)) exportTag = -1;
		gfx_text(42, 14, 0.55f, C_TEXT, "Export  (frame %d of %d, %dx%d)", doc.curFrame + 1, doc.frameCount, doc.w, doc.h);
		const int visible = 10;
		int first = exportSel - visible + 2 < 0 ? 0 : exportSel - visible + 2;
		if (first > EXPORT_ROWS - visible) first = EXPORT_ROWS - visible;
		for (int rr = first; rr < first + visible; rr++)
		{
			float y = 36 + (rr - first) * 17;
			if (rr == exportSel)
				C2D_DrawRectSolid(36, y - 2, 0.5f, 328, 16, C_SEL);
			u32 c = rr == exportSel ? C_ACCENT : C_TEXT;
			int r = rr - EXPORT_HEAD;
			if (rr == 0) { gfx_text(46, y, 0.48f, c, "Format"); gfx_text(150, y, 0.48f, c, "< %s >", EXF_NAMES[exportFmt]); continue; }
			if (rr == 1)
			{
				gfx_text(46, y, 0.48f, export_one_frame() ? C_DIM : c, "Frames");
				if (export_one_frame()) gfx_text(150, y, 0.48f, C_DIM, "current frame");
				else if (exportTag == -2)
					gfx_text(150, y, 0.48f, c, "< each tag, own files (%d) >", doc.tagCount);
				else if (exportTag >= 0)
					gfx_text(150, y, 0.48f, c, "< tag \"%s\" %d-%d >", doc.tags[exportTag].name, doc.tags[exportTag].from + 1,
					         doc.tags[exportTag].to + 1);
				else gfx_text(150, y, 0.48f, c, "< all (1-%d) >", doc.frameCount);
				continue;
			}
			if (rr == 2)
			{
				gfx_text(46, y, 0.48f, exportFmt == EXF_SHEET ? c : C_DIM, "Layout");
				gfx_text(150, y, 0.48f, exportFmt == EXF_SHEET ? c : C_DIM, exportFmt == EXF_SHEET ? "< %s >" : "%s",
				         exportFmt == EXF_SHEET ? LAYOUT_NAMES[exportLayout] : "sprite sheets only");
				continue;
			}
			if (rr == 3 || rr == 4)
			{
				u32 sc = exportFmt == EXF_SHEET ? c : C_DIM;
				gfx_text(46, y, 0.48f, sc, "%s", rr == 3 ? "Padding" : "Trim");
				if (exportFmt != EXF_SHEET) gfx_text(150, y, 0.48f, C_DIM, "sprite sheets only");
				else if (rr == 3 && sheetPad) gfx_text(150, y, 0.48f, sc, "< %d px between frames >", sheetPad);
				else if (rr == 3) gfx_text(150, y, 0.48f, sc, "< none >");
				else gfx_text(150, y, 0.48f, sc, "< %s >", sheetTrim ? "On: cut the empty border" : "Off");
				continue;
			}
			if (r == 0)
				gfx_text(46, y, 0.48f, c, "%s Recommended  (1x 2x 4x 8x)", export_is_recommended() ? "[x]" : "[  ]");
			else if (rr == EXPORT_ROWS - 1)
				gfx_text(46, y, 0.48f, c, ">> Export now");
			else
			{
				int i = r - 1, w, h;
				bool even, ok = export_dims(i, &w, &h, &even);
				char label[16];
				if (EXPORT_SIZES[i].mult) snprintf(label, sizeof(label), "%dx", EXPORT_SIZES[i].mult);
				else snprintf(label, sizeof(label), "%d px", EXPORT_SIZES[i].fixed);
				gfx_text(46, y, 0.48f, c, "%s %-7s %dx%d%s", exportOn[i] ? "[x]" : "[  ]", label, w, h,
				         !even && exportFmt != EXF_PNG ? "  (PNG only)" : !ok ? "  too big" : !even ? "  (uneven pixels)" : "");
			}
		}
		gfx_text(42, 214, 0.42f, C_DIM, "Up/Down: choose   Left/Right: change   A: tick / run   B: back");
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
		gfx_text(306, 222, 0.36f, C_DIM, "%.1f / %d MB", doc_pixel_bytes(&doc) / 1048576.0f, (int)(PIXEL_BUDGET >> 20));
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
			if (!L->isFolder)
			{
				if (layerThumbsDirty) make_layer_thumbs();
				float ts = fminf(20.0f / layerThumbW[i], 11.0f / layerThumbH[i]);
				gfx_draw_thumb(i, layerThumbW[i], layerThumbH[i], x, y, ts);
				x += 24;
			}
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
		draw_list(importCount == 64 ? "Import (first 64 files)" : "Import", labels, importCount, importSel,
		          "Put PNG, GIF, .hex or .gpl files in\n/3ds/pixel-crow/import");
	}
	else if (mode == MODE_IMPORT_AS)
		draw_list(importFiles[importSel], IMPORT_AS, IMPORT_AS_COUNT, importAsSel, "");
	else if (mode == MODE_COLORS)
		draw_list("Colors", COLORS_ITEMS, COLORS_COUNT, colorsSel, "");
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
		C2D_DrawRectSolid(50, 6, 0.5f, 300, 228, C_PANEL);
		gfx_rect_outline(50, 6, 300, 228, 1, C_ACCENT);
		gfx_text(62, 10, 0.52f, C_TEXT, "View & settings");
		for (int r = 0; r < VIEW_ROWS; r++)
		{
			float y = 30 + r * 15.5f;
			if (r == viewSel) C2D_DrawRectSolid(56, y - 2, 0.5f, 288, 15, C_SEL);
			gfx_text(66, y, 0.44f, r == viewSel ? C_ACCENT : C_TEXT, "%s", VIEW_LABELS[r]);
			char v[32];
			view_value(r, v, sizeof(v));
			gfx_text(176, y, 0.44f, C_TEXT, "< %s >", v);
		}
		gfx_text(62, 219, 0.4f, C_DIM, "Up/Down: row   Left/Right: change   A/B: close   (kept between launches)");
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

// ---- settings kept between launches ------------------------------------------------------
// X(key, variable, lowest, highest). Add new ones at the end; old files stay readable.
#define SETTINGS_LIST(X) \
	X("grid", gridOn, 0, 1) \
	X("onion", onionOn, 0, 1) \
	X("onion_frames", onionCount, 1, 3) \
	X("onion_opacity", onionPct, 10, 80) \
	X("flip_preview", flipPreview, 0, 1) \
	X("tile_preview", tilePreview, 0, 1) \
	X("hud_grid", hudGrid, 0, 32) \
	X("iso_grid", isoGrid, 0, 16) \
	X("autosave_min", autosaveMin, 0, 5) \
	X("left_handed", inputMap.lefty, 0, 1) \
	X("frame_bar_hidden", barHidden, 0, 1) \
	X("tile_drawing", wrapDraw, 0, 1) \
	X("export_format", exportFmt, 0, EXF_COUNT - 1) \
	X("export_layout", exportLayout, 0, SHEET_LAYOUT_COUNT - 1) \
	X("export_sizes", exportMask, 0, (1 << EXPORT_SIZE_COUNT) - 1) \
	X("sheet_padding", sheetPad, 0, 8) \
	X("sheet_trim", sheetTrim, 0, 1) \
	X("pencil_size", brushes[TOOL_PENCIL].size, 1, BRUSH_MAX) \
	X("pencil_brush", brushes[TOOL_PENCIL].shape, 0, BRUSH_SHAPE_COUNT - 1) \
	X("pencil_pixel_perfect", brushes[TOOL_PENCIL].pixelPerfect, 0, 1) \
	X("eraser_size", brushes[TOOL_ERASER].size, 1, BRUSH_MAX) \
	X("eraser_brush", brushes[TOOL_ERASER].shape, 0, BRUSH_SHAPE_COUNT - 1) \
	X("eraser_line", eraserLine, 0, 1) \
	X("shade_size", brushes[TOOL_SHADE].size, 1, BRUSH_MAX) \
	X("shade_brush", brushes[TOOL_SHADE].shape, 0, BRUSH_SHAPE_COUNT - 1) \
	X("shade_brightness", shadeBrightness, 0, 1) \
	X("line_size", brushes[TOOL_LINE].size, 1, BRUSH_MAX) \
	X("line_brush", brushes[TOOL_LINE].shape, 0, 1) \
	X("line_curve", lineCurve, 0, 1) \
	X("rect_size", brushes[TOOL_RECT].size, 1, BRUSH_MAX) \
	X("rect_filled", shapeFilled[TOOL_RECT], 0, 1) \
	X("ellipse_size", brushes[TOOL_ELLIPSE].size, 1, BRUSH_MAX) \
	X("ellipse_filled", shapeFilled[TOOL_ELLIPSE], 0, 1) \
	X("fill_mode", fillMode, 0, FILL_MODE_COUNT - 1) \
	X("select_mode", selMode, 0, SELMODE_COUNT - 1) \
	X("wand_all", wandAll, 0, 1) \
	X("wand_tolerance", wandTol, 0, 100) \
	X("wand_sample_all", wandSampleAll, 0, 1) \
	X("mirror_axis", mirrorMode, MIRROR_X, MIRROR_XY) \
	X("transform_crisp", tfCrisp, 0, 1) \
	X("text_crisp", textCrisp, 0, 1) \
	X("text_font", textFont, 0, FONT_COUNT - 1) \
	X("text_spacing", textSpacing, -1, 4) \
	X("frames_large_preview", bigPreview, 0, 1)
#define SETTINGS_MAX 64

static char settingsSaved[2048];   // the file's contents, to save only when something changed

// load = read the file into the variables; otherwise write it if anything differs.
static void settings_sync(bool load)
{
	int vals[SETTINGS_MAX], n = 0, exportMask = 0;
	Setting items[SETTINGS_MAX];
	for (int i = 0; i < EXPORT_SIZE_COUNT; i++) exportMask |= exportOn[i] << i;
#define X(key, var, lo, hi) vals[n] = (int)(var); items[n] = (Setting){ key, &vals[n], lo, hi }; n++;
	SETTINGS_LIST(X)
#undef X
	if (load)
	{
		if (!settings_load(SETTINGS_PATH, items, n)) return;
		n = 0;
#define X(key, var, lo, hi) var = vals[n++];
		SETTINGS_LIST(X)
#undef X
		for (int i = 0; i < EXPORT_SIZE_COUNT; i++) exportOn[i] = (exportMask >> i) & 1;
		settings_format(items, n, settingsSaved, sizeof(settingsSaved));
		return;
	}
	char now[sizeof(settingsSaved)];
	settings_format(items, n, now, sizeof(now));
	if (!strcmp(now, settingsSaved)) return;
	ensure_dirs();
	if (settings_save(SETTINGS_PATH, items, n)) memcpy(settingsSaved, now, sizeof(now));
}

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

	// moved from "Pixel UI Designer": bring the old save folder along once
	{
		struct stat st;
		if (stat(OLD_SAVE_ROOT, &st) == 0 && stat(SAVE_ROOT, &st) != 0)
			rename(OLD_SAVE_ROOT, SAVE_ROOT);
	}
	hidSetRepeatParameters(24, 5);   // held D-pad repeats (frames panel)
	export_recommended();
	settings_sync(true);
	// Old 3DS / 2DS have no ZL and ZR: SELECT (held) and R + L stand in for them
	APT_CheckNew3DS(&isNew3DS);
	inputMap.old3ds = !isNew3DS;
	view_fit();
	after_doc_change();
	curColor = doc.swatches[0];
	if (!isNew3DS) set_status("Old 3DS (untested!): hold SELECT for ZR, R + L to redo");
	bool hadAutosave = file_exists(AUTOSAVE_PATH);
	if (hadAutosave)
	{
		// Left over from a crash or power-off: turn it into a normal project right away, so
		// starting something new, opening another project or exiting can't delete it.
		char nm[DOC_NAME_MAX], to[160];
		for (int k = 1; k < 100; k++)
		{
			if (k == 1) snprintf(nm, sizeof(nm), "recovered");
			else snprintf(nm, sizeof(nm), "recovered_%d", k);
			project_path(to, sizeof(to), nm);
			if (!file_exists(to)) break;
		}
		if (!file_exists(to) && rename(AUTOSAVE_PATH, to) == 0)
		{
			char msg[96];
			snprintf(msg, sizeof(msg), "Unsaved work was recovered: \"%s\" in the Gallery", nm);
			set_status(msg);
		}
		else set_status("Unsaved work was recovered: it's first in the Gallery");
	}
	open_home(false);

	while (running && aptMainLoop())
	{
		hidScanInput();
		u32 kDown = hidKeysDown(), kHeld = hidKeysHeld(), kUp = hidKeysUp();
		touchPosition touch;
		hidTouchRead(&touch);

		if (mode == MODE_DRAW)
		{
			// left-handed layout and Old 3DS stand-ins apply while drawing; menus keep the labels they show
			uint32_t dn = kDown, hl = kHeld, rel = kUp;
			input_map(&inputMap, &dn, &hl, &rel);
			kDown = dn; kHeld = hl; kUp = rel;
			update_draw(kDown, kHeld, kUp, touch);
		}
		else if (mode == MODE_PICKER)
		{
			PickResult r = picker_update(kDown, kHeld, touch);
			if (r != PICK_OPEN)
			{
				if (r == PICK_LIST)
				{
					uint32_t cols[DOC_PALETTE_SIZE];
					int n = picker_list(cols);
					// one undo step either way: the whole document if pixels are recolored too
					bool recolored = false;
					if (doc.linkedSwatches)
					{
						hist_begin(&doc);
						recolored = doc_recolor(&doc, doc.swatches, cols, n);
						if (recolored) { hist_commit(); after_doc_change(); }
						else hist_abort();
					}
					if (!recolored) hist_push_meta(&doc, 0);
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
						bool recolored = false;
						if (doc.linkedSwatches)
						{
							hist_begin(&doc);
							recolored = doc_recolor(&doc, &doc.swatches[curSwatch], &curColor, 1);
							if (recolored) { hist_commit(); after_doc_change(); }
							else hist_abort();
						}
						if (!recolored) hist_push_meta(&doc, 0);
						doc.swatches[curSwatch] = curColor;
						doc.dirty = true;
					}
				}
				mode = MODE_DRAW;
				touchTarget = (kHeld & KEY_TOUCH) ? TOUCH_STRIP : TOUCH_NONE; // don't draw with the closing tap
			}
		}
		else if (mode == MODE_OPTIONS)
		{
			update_options(kDown);
			if (mode == MODE_OPTIONS && optTool == TOOL_MIRROR && (kHeld & KEY_TOUCH) && touch.py >= STRIP_H)
			{
				// touch the canvas to place the mirror axis (snaps to pixel edges and centres)
				float fx = (touch.px - canvas_sx()) / zoom(), fy = (touch.py - canvas_sy()) / zoom();
				int ax = (int)lroundf(fx * 2), ay = (int)lroundf(fy * 2);
				if (mirrorMode == MIRROR_X || mirrorMode == MIRROR_XY)
					mirrorAX2 = ax < 1 ? 1 : ax > doc.w * 2 - 1 ? doc.w * 2 - 1 : ax;
				if (mirrorMode == MIRROR_Y || mirrorMode == MIRROR_XY)
					mirrorAY2 = ay < 1 ? 1 : ay > doc.h * 2 - 1 ? doc.h * 2 - 1 : ay;
			}
		}
		else if (mode == MODE_SHIFT)
			update_shift(kDown, kHeld, touch);
		else if (mode == MODE_HOME)
			update_home(kDown, kHeld, touch);
		else if (mode >= MODE_LAYERS)
			update_more_menus(kDown, kHeld, touch);
		else
			update_menus(kDown);
		if (mode == MODE_HOME)
			gallery_load_some();

		if (statusTimer > 0)
			statusTimer--;
		if (textOn && !sel_floating())
			textOn = false;      // placed or cancelled elsewhere (menu, undo)

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
		// remember changed settings (checked about once a second, written only when different)
		static unsigned settingsTick;
		if (++settingsTick % 64 == 0 && touchTarget == TOUCH_NONE && (mode == MODE_DRAW || mode == MODE_HOME))
			settings_sync(false);
		if (mode != MODE_HOME) update_anim();
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

	settings_sync(false);
	doc_free(&doc);
	hist_clear();
	gfx_fini();
	C2D_Fini();
	C3D_Fini();
	gfxExit();
	return 0;
}
