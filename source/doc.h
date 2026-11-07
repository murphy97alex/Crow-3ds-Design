// Document model: canvas size, palette, frames of indexed pixels, undo history.
// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define DOC_MAX_W 320
#define DOC_MAX_H 240
#define DOC_PALETTE_SIZE 32
#define DOC_MAX_FRAMES 64
#define DOC_NAME_MAX 48

// Pixel values: 0 = transparent, 1..DOC_PALETTE_SIZE = palette slot (value - 1).
#define PIX_TRANSPARENT 0

typedef struct {
	uint16_t w, h;
	uint16_t fps;                         // playback speed for v2 animation
	uint32_t palette[DOC_PALETTE_SIZE];   // 0xRRGGBBAA
	int frameCount;
	int curFrame;
	uint8_t* frames[DOC_MAX_FRAMES];      // each w*h bytes
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

// Undo/redo: whole-frame + palette snapshots, taken before each edit.
void hist_clear(void);
void hist_push(const Doc* d);     // call before an edit; clears redo
// For strokes: snapshot at stroke start, commit only if something changed.
void hist_begin(const Doc* d);
void hist_commit(void);
void hist_abort(void);
bool hist_undo(Doc* d);
bool hist_redo(Doc* d);
