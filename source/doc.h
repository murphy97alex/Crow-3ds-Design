// Document model: canvas size, swatches, color table, frames of indexed pixels, undo history.
// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DOC_MAX_W 320
#define DOC_MAX_H 240
#define DOC_PALETTE_SIZE 32   // swatches shown in the palette strip
#define DOC_MAX_COLORS 255    // distinct colors a drawing can use
#define DOC_MAX_FRAMES 64
#define DOC_NAME_MAX 48

// Pixel values: 0 = transparent, 1..colorCount = colors[value].
// Swatches are just saved colors (like Photoshop/Clip Studio): editing one never changes the art.
#define PIX_TRANSPARENT 0

typedef struct {
	uint16_t w, h;
	uint16_t fps;                         // playback speed for v2 animation
	uint32_t swatches[DOC_PALETTE_SIZE];  // 0xRRGGBBAA
	uint32_t colors[DOC_MAX_COLORS + 1];  // color table for pixel values; [0] unused
	int colorCount;
	int frameCount;
	int curFrame;
	uint8_t* frames[DOC_MAX_FRAMES];      // each w*h bytes
	bool linkedSwatches;                  // editing a swatch also recolors matching pixels
	char name[DOC_NAME_MAX];              // "" = never saved
	bool dirty;
} Doc;

extern const uint32_t DOC_DEFAULT_PALETTE[DOC_PALETTE_SIZE];

bool doc_new(Doc* d, int w, int h);
void doc_free(Doc* d);

static inline uint8_t* doc_pixels(Doc* d) { return d->frames[d->curFrame]; }
static inline uint8_t doc_get(const Doc* d, int x, int y)
{
	return d->frames[d->curFrame][y * d->w + x];
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
// anything changed. Renumbers nothing: merged entries are left unused until the next compact.
bool doc_recolor(Doc* d, const uint32_t* from, const uint32_t* to, int n);

// Undo/redo: whole-frame + color table snapshots, taken before each edit.
void hist_clear(void);
void hist_push(const Doc* d);     // call before an edit; clears redo
// For strokes: snapshot at stroke start, commit only if something changed.
void hist_begin(const Doc* d);
void hist_commit(void);
void hist_abort(void);
bool hist_undo(Doc* d);
bool hist_redo(Doc* d);
