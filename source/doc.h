// Document model: canvas size, swatches, color table, layers (with folders) x frames of
// indexed pixels, and whole-document undo history.
// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DOC_MAX_W 320
#define DOC_MAX_H 240
#define DOC_PALETTE_SIZE 32   // swatches shown in the palette strip
#define DOC_MAX_COLORS 255    // distinct colors a drawing can use
#define DOC_MAX_FRAMES 64
#define DOC_MAX_LAYERS 16     // layers + folders
#define DOC_NAME_MAX 48
#define LAYER_NAME_MAX 16
#define DOC_MAX_TAGS 16
#define TAG_NAME_MAX 16

// Animation tag: a named range of frames that plays forward, reverse or ping-pong.
typedef enum { TAG_FORWARD, TAG_REVERSE, TAG_PINGPONG, TAG_DIR_COUNT } TagDir;
typedef struct {
	char name[TAG_NAME_MAX];
	uint8_t from, to;    // frame indices, inclusive
	uint8_t dir;         // TagDir
} Tag;

// Pixel values: 0 = transparent, 1..colorCount = colors[value].
// Swatches are just saved colors (like Photoshop/Clip Studio): editing one never changes the art.
#define PIX_TRANSPARENT 0

// Layers are stored bottom (index 0) to top. A folder is a header entry; its members sit
// directly below it (lower indices) and have `folder` set to the folder's id. One level only.
typedef struct {
	char name[LAYER_NAME_MAX];
	int id;                         // stable id (folders are referenced by id)
	int folder;                     // id of the containing folder, or -1
	bool isFolder, visible, locked, collapsed;
	uint8_t opacity;                // 0..255
	uint8_t* cels[DOC_MAX_FRAMES];  // one pixel buffer per frame; NULL for folders. Linked cels:
	                                // several frames point at the same buffer (one drawing).
} Layer;

typedef struct {
	uint16_t w, h;
	uint16_t fps;                         // playback speed for v2 animation
	uint32_t swatches[DOC_PALETTE_SIZE];  // 0xRRGGBBAA
	uint32_t colors[DOC_MAX_COLORS + 1];  // color table for pixel values; [0] unused
	int colorCount;
	int frameCount, curFrame;
	uint16_t frameMs[DOC_MAX_FRAMES];     // per-frame duration in ms; 0 = 1000 / fps
	Tag tags[DOC_MAX_TAGS];
	int tagCount;
	Layer layers[DOC_MAX_LAYERS];
	int layerCount, curLayer;             // curLayer is always a drawable (non-folder) layer
	int nextLayerId;
	bool linkedSwatches;                  // editing a swatch also recolors matching pixels
	char name[DOC_NAME_MAX];              // "" = never saved
	bool dirty;
} Doc;

extern const uint32_t DOC_DEFAULT_PALETTE[DOC_PALETTE_SIZE];

bool doc_new(Doc* d, int w, int h);
void doc_free(Doc* d);

static inline uint8_t* doc_pixels(Doc* d) { return d->layers[d->curLayer].cels[d->curFrame]; }
static inline uint8_t doc_get(const Doc* d, int x, int y)
{
	return d->layers[d->curLayer].cels[d->curFrame][y * d->w + x];
}
static inline uint32_t doc_color(const Doc* d, uint8_t v)
{
	return v == PIX_TRANSPARENT || v > d->colorCount ? 0 : d->colors[v];
}

// Pixel value for a color: finds it in the color table or adds it. If the table is full and
// allowCompact is set, unused entries are removed first (renumbers pixels, so don't allow it
// while other code holds copies of pixel values); otherwise the nearest existing color is used.
uint8_t doc_color_index(Doc* d, uint32_t rgba, bool allowCompact);
void doc_compact(Doc* d);

// Change every pixel of color from[i] to to[i] (all at once, so swaps work). Returns true if
// anything changed.
bool doc_recolor(Doc* d, const uint32_t* from, const uint32_t* to, int n);

// ---- layers ---------------------------------------------------------------------

int doc_layer_index(const Doc* d, int id);           // index of a layer/folder id, or -1
// Visibility / opacity / lock including the containing folder.
bool doc_layer_visible(const Doc* d, int i);
bool doc_layer_locked(const Doc* d, int i);
int doc_layer_opacity(const Doc* d, int i);           // 0..255
// Number of members of folder at index i (they are at i-1 .. i-count).
int doc_folder_size(const Doc* d, int i);

// Each returns the index of the new/moved entry, or -1 if not possible.
int doc_layer_add(Doc* d, bool folder);               // above the current layer
int doc_layer_duplicate(Doc* d, int i);
int doc_layer_move(Doc* d, int i, int dir);           // +1 up, -1 down (in/out of folders)
bool doc_layer_delete(Doc* d, int i);                 // folders delete their members too
bool doc_layer_merge_down(Doc* d, int i);
bool doc_merge_visible(Doc* d);
void doc_set_current_layer(Doc* d, int i);            // ignores folders

// Composite visible layers of a frame into RGBA (0xRRGGBBAA, straight alpha).
void doc_composite(const Doc* d, int frame, uint32_t* out);

// ---- frames ---------------------------------------------------------------------

int doc_frame_ms(const Doc* d, int f);                  // duration of frame f in ms
size_t doc_pixel_bytes(const Doc* d);                   // memory used by all pixel buffers
// Insert a frame at index `at`: blank (copyFrom < 0) or a copy of frame copyFrom. Becomes current.
bool doc_frame_insert(Doc* d, int at, int copyFrom);
bool doc_frame_delete(Doc* d, int f);                   // keeps at least one frame
bool doc_frame_move(Doc* d, int f, int dir);            // swap with the neighbour (-1 / +1)
// Linked cels: frame f of layer li shows the same drawing as frame f-1 (editing one edits both).
bool doc_cel_link_prev(Doc* d, int li, int f);
bool doc_cel_unlink(Doc* d, int li, int f);             // give frame f its own copy
bool doc_cel_linked(const Doc* d, int li, int f);       // shared with any other frame?

// ---- canvas ---------------------------------------------------------------------

// New size; ax/ay = anchor 0 (left/top), 1 (center), 2 (right/bottom).
bool doc_resize(Doc* d, int w, int h, int ax, int ay);
bool doc_crop(Doc* d, int x, int y, int w, int h);

// ---- undo -----------------------------------------------------------------------
// Snapshots cover the whole document (pixels, layers, color table, size); swatches excluded.
void hist_clear(void);
void hist_push(const Doc* d);     // call before an edit; clears redo
// For strokes: snapshot at stroke start, commit only if something changed.
void hist_begin(const Doc* d);
// Cheaper snapshot for edits that only touch the current layer's current cel (strokes, moves).
void hist_begin_cel(const Doc* d);
void hist_commit(void);
void hist_abort(void);
bool hist_undo(Doc* d);
bool hist_redo(Doc* d);
