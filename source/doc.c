// SPDX-License-Identifier: MIT
#include "doc.h"
#include <stdlib.h>
#include <string.h>

// Placeholder starting palette (original, not from any published palette):
// grays, then ramps of red, orange, yellow, green, teal, blue, purple, pink, brown.
const uint32_t DOC_DEFAULT_PALETTE[DOC_PALETTE_SIZE] = {
	0x000000FF, 0x3A3A44FF, 0x6E6E7AFF, 0xA8A8B4FF, 0xDCDCE4FF, 0xFFFFFFFF,
	0x5A1A1EFF, 0xA82A2AFF, 0xE8463CFF, 0xFF8C78FF,
	0xB85A14FF, 0xF08C28FF, 0xFFC864FF,
	0xC8A01EFF, 0xF8E040FF, 0xFFF4A0FF,
	0x1E5A28FF, 0x3C9A3CFF, 0x78D25AFF, 0xC4F096FF,
	0x16626AFF, 0x28B4B4FF, 0x8CF0E6FF,
	0x1E2C78FF, 0x3060D0FF, 0x64A8FFFF,
	0x4A2478FF, 0x8C46C8FF, 0xC896F0FF,
	0xD2508CFF, 0x6E4628FF, 0xB48250FF,
};

bool doc_new(Doc* d, int w, int h)
{
	if (w < 1 || h < 1 || w > DOC_MAX_W || h > DOC_MAX_H)
		return false;
	uint8_t* px = calloc((size_t)w * h, 1);
	if (!px)
		return false;
	doc_free(d);
	d->w = w;
	d->h = h;
	d->fps = 8;
	memcpy(d->palette, DOC_DEFAULT_PALETTE, sizeof(d->palette));
	d->frameCount = 1;
	d->curFrame = 0;
	d->frames[0] = px;
	d->name[0] = '\0';
	d->dirty = false;
	hist_clear();
	return true;
}

void doc_free(Doc* d)
{
	for (int i = 0; i < DOC_MAX_FRAMES; i++)
	{
		free(d->frames[i]);
		d->frames[i] = NULL;
	}
	d->frameCount = 0;
}

// ---- history ---------------------------------------------------------------

#define HIST_MAX 48

typedef struct {
	int frame;
	uint32_t palette[DOC_PALETTE_SIZE];
	uint8_t* px;
} Snap;

static Snap undoStack[HIST_MAX], redoStack[HIST_MAX];
static int undoCount, redoCount;

static void snap_free(Snap* s) { free(s->px); s->px = NULL; }

static void stack_clear(Snap* st, int* n)
{
	for (int i = 0; i < *n; i++)
		snap_free(&st[i]);
	*n = 0;
}

void hist_abort(void);

void hist_clear(void)
{
	hist_abort();
	stack_clear(undoStack, &undoCount);
	stack_clear(redoStack, &redoCount);
}

static bool snap_take(Snap* s, const Doc* d)
{
	size_t n = (size_t)d->w * d->h;
	s->px = malloc(n);
	if (!s->px)
		return false;
	memcpy(s->px, d->frames[d->curFrame], n);
	memcpy(s->palette, d->palette, sizeof(s->palette));
	s->frame = d->curFrame;
	return true;
}

static void snap_restore(Snap* s, Doc* d)
{
	memcpy(d->frames[s->frame], s->px, (size_t)d->w * d->h);
	memcpy(d->palette, s->palette, sizeof(d->palette));
	d->curFrame = s->frame;
	snap_free(s);
}

static void stack_push(Snap* st, int* n, Snap s)
{
	if (*n == HIST_MAX)
	{
		snap_free(&st[0]);
		memmove(&st[0], &st[1], sizeof(Snap) * (HIST_MAX - 1));
		(*n)--;
	}
	st[(*n)++] = s;
}

void hist_push(const Doc* d)
{
	Snap s;
	if (!snap_take(&s, d))
		return;
	stack_push(undoStack, &undoCount, s);
	stack_clear(redoStack, &redoCount);
}

static Snap pending;
static bool hasPending;

void hist_begin(const Doc* d)
{
	hist_abort();
	hasPending = snap_take(&pending, d);
}

void hist_commit(void)
{
	if (!hasPending)
		return;
	stack_push(undoStack, &undoCount, pending);
	stack_clear(redoStack, &redoCount);
	hasPending = false;
}

void hist_abort(void)
{
	if (hasPending)
		snap_free(&pending);
	hasPending = false;
}

static bool hist_move(Doc* d, Snap* from, int* nFrom, Snap* to, int* nTo)
{
	if (*nFrom == 0)
		return false;
	d->curFrame = from[*nFrom - 1].frame;
	Snap cur;
	if (!snap_take(&cur, d))
		return false;
	Snap s = from[--(*nFrom)];
	snap_restore(&s, d);
	stack_push(to, nTo, cur);
	d->dirty = true;
	return true;
}

bool hist_undo(Doc* d) { return hist_move(d, undoStack, &undoCount, redoStack, &redoCount); }
bool hist_redo(Doc* d) { return hist_move(d, redoStack, &redoCount, undoStack, &undoCount); }
