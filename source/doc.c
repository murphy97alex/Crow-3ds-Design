// SPDX-License-Identifier: MIT
#include "doc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Default palette for new projects. Original to this project and released under CC0 1.0
// (see docs/PALETTE.md). Row 1: grays, skin/browns, reds, oranges/yellows.
// Row 2: greens, teals, blues, purples, pinks.
const uint32_t DOC_DEFAULT_PALETTE[DOC_PALETTE_SIZE] = {
	0x000000FF, 0x2B2C34FF, 0x565966FF, 0x8B8E9BFF, 0xC3C6CFFF, 0xFFFFFFFF,
	0x3E2618FF, 0x7A4A2CFF, 0xC07E50FF, 0xF0C29AFF,
	0x6E1A26FF, 0xC02C34FF, 0xF2604CFF,
	0xE88A24FF, 0xF8C838FF, 0xFFF29AFF,
	0x1C4A2EFF, 0x2E8A3EFF, 0x6CC650FF, 0xC6EE8EFF,
	0x145A66FF, 0x22A6B0FF, 0x7CE8E6FF,
	0x1A2050FF, 0x2A4CB0FF, 0x3E8AF0FF, 0x9CCCFFFF,
	0x3E1E5CFF, 0x7A3EB4FF, 0xC08AF0FF,
	0xB8307EFF, 0xF27AB8FF,
};

static void layer_init(Doc* d, Layer* L, bool folder)
{
	memset(L, 0, sizeof(*L));
	L->id = d->nextLayerId++;
	L->folder = -1;
	L->isFolder = folder;
	L->visible = true;
	L->opacity = 255;
	snprintf(L->name, sizeof(L->name), "%s %d", folder ? "Folder" : "Layer", L->id);
}

static bool layer_alloc(Doc* d, Layer* L)
{
	if (L->isFolder)
		return true;
	for (int f = 0; f < d->frameCount; f++)
		if (!(L->cels[f] = calloc((size_t)d->w * d->h, 1)))
			return false;
	return true;
}

// Is frame f's buffer the same as an earlier frame's (a linked cel already counted)?
static bool cel_dup(const Layer* L, int f)
{
	for (int g = 0; g < f; g++)
		if (L->cels[g] == L->cels[f]) return true;
	return false;
}

static void layer_free(Layer* L)
{
	for (int f = 0; f < DOC_MAX_FRAMES; f++)
		if (L->cels[f] && !cel_dup(L, f))
			free(L->cels[f]);
	for (int f = 0; f < DOC_MAX_FRAMES; f++)
		L->cels[f] = NULL;
}

// Copy a layer's buffers, keeping linked cels linked. Returns false on out of memory.
static bool cels_clone(Layer* dst, const Layer* src, size_t n)
{
	for (int f = 0; f < DOC_MAX_FRAMES; f++)
	{
		dst->cels[f] = NULL;
		if (!src->cels[f]) continue;
		int g;
		for (g = 0; g < f && src->cels[g] != src->cels[f]; g++) {}
		if (g < f) { dst->cels[f] = dst->cels[g]; continue; }
		if (!(dst->cels[f] = malloc(n)))
		{
			for (int k = f; k < DOC_MAX_FRAMES; k++) dst->cels[k] = NULL;
			layer_free(dst);
			return false;
		}
		memcpy(dst->cels[f], src->cels[f], n);
	}
	return true;
}

bool doc_new(Doc* d, int w, int h)
{
	if (w < 1 || h < 1 || w > DOC_MAX_W || h > DOC_MAX_H)
		return false;
	uint8_t* first = calloc((size_t)w * h, 1);   // before touching d: a failure keeps the old document
	if (!first)
		return false;
	doc_free(d);
	d->w = w;
	d->h = h;
	d->fps = 8;
	memcpy(d->swatches, DOC_DEFAULT_PALETTE, sizeof(d->swatches));
	d->colorCount = 0;
	d->frameCount = 1;
	d->curFrame = 0;
	memset(d->frameMs, 0, sizeof(d->frameMs));
	d->tagCount = 0;
	d->nextLayerId = 1;
	d->layerCount = 1;
	d->curLayer = 0;
	layer_init(d, &d->layers[0], false);
	d->layers[0].cels[0] = first;
	d->linkedSwatches = false;
	d->name[0] = '\0';
	d->dirty = false;
	hist_clear();
	return true;
}

void doc_free(Doc* d)
{
	for (int i = 0; i < DOC_MAX_LAYERS; i++)
		layer_free(&d->layers[i]);
	d->layerCount = 0;
	d->frameCount = 0;
}

// ---- colors ----------------------------------------------------------------------

static long color_dist(uint32_t a, uint32_t b)
{
	long dr = (long)((a >> 24) & 255) - ((b >> 24) & 255);
	long dg = (long)((a >> 16) & 255) - ((b >> 16) & 255);
	long db = (long)((a >> 8) & 255) - ((b >> 8) & 255);
	return dr * dr * 3 + dg * dg * 4 + db * db * 2;
}

// Calls fn on every pixel buffer of the document.
// Every pixel buffer once (linked cels are visited only the first time).
#define FOR_EACH_CEL(d, cel)                                              \
	for (int _l = 0; _l < (d)->layerCount; _l++)                          \
		for (int _f = 0; _f < (d)->frameCount; _f++)                      \
			for (uint8_t* cel = cel_dup(&(d)->layers[_l], _f) ? NULL : (d)->layers[_l].cels[_f]; cel; cel = NULL)

void doc_compact(Doc* d)
{
	bool used[DOC_MAX_COLORS + 1] = { false };
	size_t n = (size_t)d->w * d->h;
	FOR_EACH_CEL(d, cel)
		for (size_t i = 0; i < n; i++)
			used[cel[i]] = true;
	uint8_t remap[DOC_MAX_COLORS + 1] = { 0 };
	int count = 0;
	for (int v = 1; v <= d->colorCount; v++)
		if (used[v])
		{
			d->colors[++count] = d->colors[v];
			remap[v] = count;
		}
	FOR_EACH_CEL(d, cel)
		for (size_t i = 0; i < n; i++)
			cel[i] = remap[cel[i]];
	d->colorCount = count;
}

uint8_t doc_color_index(Doc* d, uint32_t rgba, bool allowCompact)
{
	rgba |= 0xFF;  // drawing colors are opaque
	for (int v = 1; v <= d->colorCount; v++)
		if (d->colors[v] == rgba)
			return v;
	if (d->colorCount >= DOC_MAX_COLORS && allowCompact)
		doc_compact(d);
	if (d->colorCount < DOC_MAX_COLORS)
	{
		d->colors[++d->colorCount] = rgba;
		return d->colorCount;
	}
	int best = 1;
	for (int v = 2; v <= d->colorCount; v++)
		if (color_dist(d->colors[v], rgba) < color_dist(d->colors[best], rgba))
			best = v;
	return best;
}

bool doc_recolor(Doc* d, const uint32_t* from, const uint32_t* to, int n)
{
	bool changed = false;
	int count = d->colorCount;
	uint32_t orig[DOC_MAX_COLORS + 1];
	memcpy(orig, d->colors, sizeof(orig));
	for (int v = 1; v <= count; v++)
		for (int i = 0; i < n; i++)
			if (orig[v] == (from[i] | 0xFF) && from[i] != to[i])
			{
				d->colors[v] = to[i] | 0xFF;
				changed = true;
				break;
			}
	if (!changed)
		return false;
	// merge entries that now hold the same color
	uint8_t remap[DOC_MAX_COLORS + 1];
	bool needRemap = false;
	for (int v = 0; v <= count; v++)
	{
		remap[v] = v;
		for (int u = 1; u < v; u++)
			if (remap[u] == u && d->colors[u] == d->colors[v]) { remap[v] = u; needRemap = true; break; }
	}
	if (needRemap)
	{
		size_t np = (size_t)d->w * d->h;
		FOR_EACH_CEL(d, cel)
			for (size_t i = 0; i < np; i++)
				cel[i] = remap[cel[i]];
	}
	return true;
}

// ---- layers ----------------------------------------------------------------------

int doc_layer_index(const Doc* d, int id)
{
	for (int i = 0; i < d->layerCount; i++)
		if (d->layers[i].id == id)
			return i;
	return -1;
}

static const Layer* folder_of(const Doc* d, int i)
{
	int f = d->layers[i].folder >= 0 ? doc_layer_index(d, d->layers[i].folder) : -1;
	return f >= 0 ? &d->layers[f] : NULL;
}

bool doc_layer_visible(const Doc* d, int i)
{
	const Layer* f = folder_of(d, i);
	return d->layers[i].visible && (!f || f->visible);
}

bool doc_layer_locked(const Doc* d, int i)
{
	const Layer* f = folder_of(d, i);
	return d->layers[i].locked || (f && f->locked);
}

int doc_layer_opacity(const Doc* d, int i)
{
	const Layer* f = folder_of(d, i);
	return f ? d->layers[i].opacity * f->opacity / 255 : d->layers[i].opacity;
}

int doc_folder_size(const Doc* d, int i)
{
	if (!d->layers[i].isFolder)
		return 0;
	int n = 0;
	for (int j = i - 1; j >= 0 && d->layers[j].folder == d->layers[i].id; j--)
		n++;
	return n;
}

static int drawable_count(const Doc* d)
{
	int n = 0;
	for (int i = 0; i < d->layerCount; i++)
		n += !d->layers[i].isFolder;
	return n;
}

void doc_set_current_layer(Doc* d, int i)
{
	if (i >= 0 && i < d->layerCount && !d->layers[i].isFolder)
		d->curLayer = i;
}

// Keep curLayer pointing at the same layer (by id) after entries move around.
static void fix_current(Doc* d, int curId)
{
	int i = doc_layer_index(d, curId);
	if (i >= 0 && !d->layers[i].isFolder) { d->curLayer = i; return; }
	// current layer was removed: nearest drawable layer
	int best = -1;
	for (int j = 0; j < d->layerCount; j++)
		if (!d->layers[j].isFolder && (best < 0 || abs(j - d->curLayer) < abs(best - d->curLayer)))
			best = j;
	d->curLayer = best < 0 ? 0 : best;
}

static void insert_at(Doc* d, int pos, const Layer* L)
{
	memmove(&d->layers[pos + 1], &d->layers[pos], sizeof(Layer) * (d->layerCount - pos));
	d->layers[pos] = *L;
	d->layerCount++;
}

static void remove_at(Doc* d, int pos)
{
	layer_free(&d->layers[pos]);
	memmove(&d->layers[pos], &d->layers[pos + 1], sizeof(Layer) * (d->layerCount - pos - 1));
	memset(&d->layers[d->layerCount - 1], 0, sizeof(Layer));
	d->layerCount--;
}

int doc_layer_add(Doc* d, bool folder)
{
	if (d->layerCount >= DOC_MAX_LAYERS)
		return -1;
	Layer L;
	layer_init(d, &L, folder);
	if (!layer_alloc(d, &L)) { layer_free(&L); return -1; }
	int cur = d->curLayer, pos;
	if (!folder)
	{
		pos = cur + 1;
		L.folder = d->layers[cur].folder;
	}
	else
	{
		int h = d->layers[cur].folder >= 0 ? doc_layer_index(d, d->layers[cur].folder) : cur;
		pos = h + 1;
	}
	insert_at(d, pos, &L);
	if (!folder)
		d->curLayer = pos;
	return pos;
}

int doc_layer_duplicate(Doc* d, int i)
{
	if (d->layerCount >= DOC_MAX_LAYERS || d->layers[i].isFolder)
		return -1;
	Layer L = d->layers[i];
	L.id = d->nextLayerId++;
	char nm[LAYER_NAME_MAX];
	snprintf(nm, sizeof(nm), "%.10s copy", d->layers[i].name);
	memcpy(L.name, nm, sizeof(nm));
	if (!cels_clone(&L, &d->layers[i], (size_t)d->w * d->h)) return -1;
	insert_at(d, i + 1, &L);
	d->curLayer = i + 1;
	return i + 1;
}

static void swap_layers(Doc* d, int a, int b)
{
	Layer t = d->layers[a];
	d->layers[a] = d->layers[b];
	d->layers[b] = t;
}

// Move layers [lo,hi] so they come after [hi+1, uhi] (i.e. swap two adjacent ranges).
static void swap_ranges(Doc* d, int lo, int mid, int hi)
{
	// ranges [lo, mid] and [mid+1, hi] -> [mid+1, hi][lo, mid]
	Layer tmp[DOC_MAX_LAYERS];
	int a = mid - lo + 1, b = hi - mid;
	memcpy(tmp, &d->layers[mid + 1], sizeof(Layer) * b);
	memcpy(tmp + b, &d->layers[lo], sizeof(Layer) * a);
	memcpy(&d->layers[lo], tmp, sizeof(Layer) * (a + b));
}

int doc_layer_move(Doc* d, int i, int dir)
{
	int curId = d->layers[d->curLayer].id;
	Layer* L = &d->layers[i];
	int result = -1;
	if (!L->isFolder)
	{
		if (dir > 0)
		{
			int j = i + 1;
			if (j >= d->layerCount) return -1;
			Layer* J = &d->layers[j];
			if (L->folder >= 0 && J->isFolder && J->id == L->folder)
			{
				swap_layers(d, i, j);            // leave the folder upward
				d->layers[j].folder = -1;
				result = j;
			}
			else if (L->folder < 0 && J->folder >= 0)
			{
				L->folder = J->folder;            // join the folder above (as its bottom member)
				result = i;
			}
			else if (L->folder < 0 && J->isFolder)
			{
				L->folder = J->id;                // join an empty folder directly above
				result = i;
			}
			else
			{
				swap_layers(d, i, j);
				result = j;
			}
		}
		else
		{
			int j = i - 1;
			if (L->folder >= 0 && (j < 0 || d->layers[j].folder != L->folder))
			{
				L->folder = -1;                   // leave the folder downward (stays in place)
				result = i;
			}
			else if (j < 0)
				return -1;
			else if (L->folder < 0 && d->layers[j].isFolder)
			{
				int fid = d->layers[j].id;        // enter the folder below, at its top
				swap_layers(d, i, j);
				d->layers[j].folder = fid;
				result = j;
			}
			else
			{
				swap_layers(d, i, j);
				result = j;
			}
		}
	}
	else
	{
		int hi = i, lo = i - doc_folder_size(d, i);
		if (dir > 0)
		{
			int j = hi + 1;
			if (j >= d->layerCount) return -1;
			int uhi = j;
			if (d->layers[j].folder >= 0)
				uhi = doc_layer_index(d, d->layers[j].folder);   // whole folder block above
			swap_ranges(d, lo, hi, uhi);
			result = uhi;
		}
		else
		{
			int j = lo - 1;
			if (j < 0) return -1;
			int ulo = j;
			if (d->layers[j].isFolder)
				ulo = j - doc_folder_size(d, j);                 // whole folder block below
			swap_ranges(d, ulo, j, hi);
			result = ulo + (hi - lo);
		}
	}
	fix_current(d, curId);
	return result;
}

bool doc_layer_delete(Doc* d, int i)
{
	int first = i, last = i;
	if (d->layers[i].isFolder)
		first = i - doc_folder_size(d, i);
	int removedDrawable = 0;
	for (int k = first; k <= last; k++)
		removedDrawable += !d->layers[k].isFolder;
	if (drawable_count(d) - removedDrawable < 1)
		return false;
	int curId = d->layers[d->curLayer].id;
	for (int k = last; k >= first; k--)
		remove_at(d, k);
	if (d->curLayer >= d->layerCount) d->curLayer = d->layerCount - 1;
	fix_current(d, curId);
	return true;
}

static uint32_t blend(uint32_t dst, uint32_t src, int a)
{
	uint32_t out = 0xFF;
	for (int sh = 8; sh <= 24; sh += 8)
	{
		int dc = (dst >> sh) & 255, sc = (src >> sh) & 255;
		out |= (uint32_t)((sc * a + dc * (255 - a)) / 255) << sh;
	}
	return out;
}

bool doc_layer_merge_down(Doc* d, int i)
{
	int j = i - 1;
	if (j < 0 || d->layers[i].isFolder || d->layers[j].isFolder)
		return false;
	int a = d->layers[i].opacity;
	size_t n = (size_t)d->w * d->h;
	for (int f = 0; f < d->frameCount; f++)
		if (!doc_cel_unlink(d, j, f)) return false;   // each frame of the result is its own drawing
	for (int f = 0; f < d->frameCount; f++)
	{
		uint8_t *src = d->layers[i].cels[f], *dst = d->layers[j].cels[f];
		for (size_t k = 0; k < n; k++)
		{
			if (!src[k]) continue;
			if (a >= 255) dst[k] = src[k];
			else if (!dst[k]) dst[k] = a >= 128 ? src[k] : 0;
			else dst[k] = doc_color_index(d, blend(d->colors[dst[k]], d->colors[src[k]], a), false);
		}
	}
	int keepId = d->layers[j].id;
	remove_at(d, i);
	fix_current(d, keepId);
	return true;
}

bool doc_merge_visible(Doc* d)
{
	int target = -1, count = 0;
	for (int i = 0; i < d->layerCount; i++)
		if (!d->layers[i].isFolder && doc_layer_visible(d, i))
		{
			if (target < 0) target = i;
			count++;
		}
	if (count < 2)
		return false;
	doc_compact(d);
	size_t n = (size_t)d->w * d->h;
	for (int f = 0; f < d->frameCount; f++)
		if (!doc_cel_unlink(d, target, f)) return false;
	uint32_t* rgba = malloc(n * 4);
	if (!rgba) return false;
	for (int f = 0; f < d->frameCount; f++)
	{
		doc_composite(d, f, rgba);
		uint8_t* dst = d->layers[target].cels[f];
		for (size_t k = 0; k < n; k++)
			dst[k] = (rgba[k] & 255) >= 128 ? doc_color_index(d, rgba[k], false) : 0;
	}
	free(rgba);
	int keepId = d->layers[target].id;
	d->layers[target].opacity = 255;
	for (int i = d->layerCount - 1; i > target; i--)
		if (!d->layers[i].isFolder && doc_layer_visible(d, i))
			remove_at(d, i);
	fix_current(d, keepId);
	return true;
}

void doc_composite(const Doc* d, int frame, uint32_t* out)
{
	doc_composite_rect(d, frame, out, 0, 0, d->w - 1, d->h - 1);
}

void doc_composite_rect(const Doc* d, int frame, uint32_t* out, int x0, int y0, int x1, int y1)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > d->w - 1) x1 = d->w - 1;
	if (y1 > d->h - 1) y1 = d->h - 1;
	if (x0 > x1 || y0 > y1) return;
	for (int y = y0; y <= y1; y++)
		memset(out + (size_t)y * d->w + x0, 0, (size_t)(x1 - x0 + 1) * 4);
	for (int i = 0; i < d->layerCount; i++)
	{
		if (d->layers[i].isFolder || !doc_layer_visible(d, i)) continue;
		int a = doc_layer_opacity(d, i);
		if (a <= 0) continue;
		const uint8_t* px = d->layers[i].cels[frame];
		for (int y = y0; y <= y1; y++)
		for (size_t k = (size_t)y * d->w + x0, kEnd = (size_t)y * d->w + x1; k <= kEnd; k++)
		{
			uint8_t v = px[k];
			if (!v) continue;
			uint32_t c = doc_color(d, v);
			if (a >= 255) { out[k] = c; continue; }
			uint32_t dst = out[k];
			int da = dst & 255;
			int oa = a + da * (255 - a) / 255;
			if (oa <= 0) continue;
			uint32_t r = (uint32_t)oa;
			for (int sh = 8; sh <= 24; sh += 8)
			{
				int sc = (c >> sh) & 255, dc = (dst >> sh) & 255;
				int v2 = (sc * a + dc * da * (255 - a) / 255) / oa;
				r |= (uint32_t)(v2 > 255 ? 255 : v2) << sh;
			}
			out[k] = r;
		}
	}
}

// ---- frames ----------------------------------------------------------------------

int doc_frame_ms(const Doc* d, int f)
{
	// never 0: playback and exports step by this
	int ms = d->frameMs[f] ? d->frameMs[f] : 1000 / (d->fps ? d->fps : 8);
	return ms < 10 ? 10 : ms;
}

size_t doc_pixel_bytes(const Doc* d)
{
	size_t n = 0;
	FOR_EACH_CEL(d, cel) n += (size_t)d->w * d->h;
	return n;
}

bool doc_frame_insert(Doc* d, int at, int copyFrom)
{
	if (d->frameCount >= DOC_MAX_FRAMES || at < 0 || at > d->frameCount) return false;
	size_t n = (size_t)d->w * d->h;
	uint8_t* fresh[DOC_MAX_LAYERS] = { 0 };
	for (int i = 0; i < d->layerCount; i++)
	{
		if (d->layers[i].isFolder) continue;
		fresh[i] = copyFrom >= 0 ? malloc(n) : calloc(n, 1);
		if (!fresh[i]) { for (int k = 0; k < i; k++) free(fresh[k]); return false; }
		if (copyFrom >= 0) memcpy(fresh[i], d->layers[i].cels[copyFrom], n);
	}
	for (int i = 0; i < d->layerCount; i++)
	{
		Layer* L = &d->layers[i];
		if (L->isFolder) continue;
		memmove(&L->cels[at + 1], &L->cels[at], sizeof(uint8_t*) * (d->frameCount - at));
		L->cels[at] = fresh[i];
	}
	memmove(&d->frameMs[at + 1], &d->frameMs[at], sizeof(uint16_t) * (d->frameCount - at));
	d->frameMs[at] = copyFrom >= 0 ? d->frameMs[copyFrom < at ? copyFrom : copyFrom + 1] : 0;
	for (int t = 0; t < d->tagCount; t++)
	{
		if (d->tags[t].from >= at) d->tags[t].from++;
		if (d->tags[t].to >= at) d->tags[t].to++;
		else if (d->tags[t].to + 1 == at && at > d->tags[t].from) d->tags[t].to++;   // growing a tag at its end
	}
	d->frameCount++;
	d->curFrame = at;
	return true;
}

bool doc_frame_delete(Doc* d, int f)
{
	if (d->frameCount <= 1 || f < 0 || f >= d->frameCount) return false;
	for (int i = 0; i < d->layerCount; i++)
	{
		Layer* L = &d->layers[i];
		if (L->isFolder) continue;
		bool shared = false;
		for (int g = 0; g < d->frameCount; g++)
			if (g != f && L->cels[g] == L->cels[f]) shared = true;
		if (!shared) free(L->cels[f]);
		memmove(&L->cels[f], &L->cels[f + 1], sizeof(uint8_t*) * (d->frameCount - f - 1));
		L->cels[d->frameCount - 1] = NULL;
	}
	memmove(&d->frameMs[f], &d->frameMs[f + 1], sizeof(uint16_t) * (d->frameCount - f - 1));
	d->frameMs[d->frameCount - 1] = 0;
	d->frameCount--;
	for (int t = 0; t < d->tagCount; t++)
	{
		Tag* T = &d->tags[t];
		if (T->from == f && T->to == f)
		{
			// the tag's only frame is gone: remove the tag
			memmove(T, T + 1, sizeof(Tag) * (d->tagCount - t - 1));
			d->tagCount--;
			t--;
			continue;
		}
		if (T->from > f) T->from--;
		if (T->to >= f) T->to--;
	}
	if (d->curFrame >= d->frameCount) d->curFrame = d->frameCount - 1;
	return true;
}

bool doc_frame_move(Doc* d, int f, int dir)
{
	int g = f + dir;
	if (g < 0 || g >= d->frameCount || f < 0 || f >= d->frameCount) return false;
	for (int i = 0; i < d->layerCount; i++)
	{
		Layer* L = &d->layers[i];
		if (L->isFolder) continue;
		uint8_t* t = L->cels[f];
		L->cels[f] = L->cels[g];
		L->cels[g] = t;
	}
	uint16_t ms = d->frameMs[f];
	d->frameMs[f] = d->frameMs[g];
	d->frameMs[g] = ms;
	for (int t = 0; t < d->tagCount; t++)
	{
		Tag* T = &d->tags[t];
		if (T->from != T->to) continue;        // longer tags keep their place in the timeline
		if (T->from == f) T->from = T->to = (uint8_t)g;
		else if (T->from == g) T->from = T->to = (uint8_t)f;
	}
	if (d->curFrame == f) d->curFrame = g;
	else if (d->curFrame == g) d->curFrame = f;
	return true;
}

bool doc_cel_copy_next(Doc* d, int li, int f)
{
	Layer* L = &d->layers[li];
	if (L->isFolder || f < 0 || f + 1 >= d->frameCount) return false;
	if (L->cels[f + 1] == L->cels[f]) return true;       // linked: already the same drawing
	if (!doc_cel_unlink(d, li, f + 1)) return false;
	memcpy(L->cels[f + 1], L->cels[f], (size_t)d->w * d->h);
	return true;
}

bool doc_cel_linked(const Doc* d, int li, int f)
{
	const Layer* L = &d->layers[li];
	if (L->isFolder) return false;
	for (int g = 0; g < d->frameCount; g++)
		if (g != f && L->cels[g] == L->cels[f]) return true;
	return false;
}

bool doc_cel_link_prev(Doc* d, int li, int f)
{
	Layer* L = &d->layers[li];
	if (L->isFolder || f <= 0 || f >= d->frameCount || L->cels[f] == L->cels[f - 1]) return false;
	if (!doc_cel_linked(d, li, f)) free(L->cels[f]);
	L->cels[f] = L->cels[f - 1];
	return true;
}

bool doc_cel_unlink(Doc* d, int li, int f)
{
	Layer* L = &d->layers[li];
	if (L->isFolder) return false;
	if (!doc_cel_linked(d, li, f)) return true;
	size_t n = (size_t)d->w * d->h;
	uint8_t* c = malloc(n);
	if (!c) return false;
	memcpy(c, L->cels[f], n);
	L->cels[f] = c;
	return true;
}

// ---- canvas ----------------------------------------------------------------------

// New size; old pixel (x, y) moves to (x + ox, y + oy).
static bool reframe(Doc* d, int w, int h, int ox, int oy)
{
	if (w < 1 || h < 1 || w > DOC_MAX_W || h > DOC_MAX_H)
		return false;
	// Build every new buffer first, so running out of memory leaves the document untouched.
	static uint8_t* fresh[DOC_MAX_LAYERS][DOC_MAX_FRAMES];
	memset(fresh, 0, sizeof(fresh));
	bool ok = true;
	for (int i = 0; ok && i < d->layerCount; i++)
	{
		Layer* L = &d->layers[i];
		if (L->isFolder) continue;
		for (int f = 0; ok && f < d->frameCount; f++)
		{
			if (cel_dup(L, f)) continue;   // linked: shares the first frame's new buffer
			uint8_t* nb = calloc((size_t)w * h, 1);
			if (!nb) { ok = false; break; }
			fresh[i][f] = nb;
			for (int y = 0; y < d->h; y++)
			{
				int ny = y + oy;
				if (ny < 0 || ny >= h) continue;
				for (int x = 0; x < d->w; x++)
				{
					int nx = x + ox;
					if (nx >= 0 && nx < w)
						nb[ny * w + nx] = L->cels[f][y * d->w + x];
				}
			}
		}
	}
	if (!ok)
	{
		for (int i = 0; i < DOC_MAX_LAYERS; i++)
			for (int f = 0; f < DOC_MAX_FRAMES; f++)
				free(fresh[i][f]);
		return false;
	}
	for (int i = 0; i < d->layerCount; i++)
	{
		Layer* L = &d->layers[i];
		if (L->isFolder) continue;
		for (int f = 0; f < d->frameCount; f++)
		{
			if (!fresh[i][f]) continue;
			uint8_t* old = L->cels[f];
			for (int g = f; g < d->frameCount; g++)   // every frame sharing this drawing
				if (L->cels[g] == old) L->cels[g] = fresh[i][f];
			free(old);
		}
	}
	d->w = w;
	d->h = h;
	return true;
}

bool doc_resize(Doc* d, int w, int h, int ax, int ay)
{
	return reframe(d, w, h, (w - d->w) * ax / 2, (h - d->h) * ay / 2);
}

bool doc_crop(Doc* d, int x, int y, int w, int h)
{
	return reframe(d, w, h, -x, -y);
}

// ---- history ---------------------------------------------------------------------

#define HIST_MAX 48
#define HIST_BUDGET (12u * 1024 * 1024)   // bytes of pixel data kept for undo/redo

// Everything undoable that isn't pixels or structure.
typedef struct {
	uint16_t fps, frameMs[DOC_MAX_FRAMES];
	Tag tags[DOC_MAX_TAGS];
	int tagCount;
	uint32_t swatches[DOC_PALETTE_SIZE];
	int layerCount;
	struct { int id; char name[LAYER_NAME_MAX]; bool visible, locked, collapsed; uint8_t opacity; } layers[DOC_MAX_LAYERS];
} Meta;

typedef struct {
	Doc* doc;      // whole-document deep copy (name and dirty flag ignored on restore)
	Meta* meta;    // or: settings only
	int key;       // meta: which setting, so repeated changes share a step
	uint8_t* cel;  // or: one cel's pixels, with the color table they refer to
	uint32_t* colors;
	int colorCount, layerId, frame;
	size_t bytes;
} Snap;

static Snap undoStack[HIST_MAX], redoStack[HIST_MAX];
static int undoCount, redoCount;
static size_t histBytes;
static Snap pending;
static bool hasPending;

static void snap_free(Snap* s)
{
	if (s->doc)
	{
		doc_free(s->doc);
		free(s->doc);
	}
	free(s->cel);
	free(s->colors);
	free(s->meta);
	s->meta = NULL;
	s->doc = NULL;
	s->cel = NULL;
	s->colors = NULL;
	histBytes -= s->bytes;
	s->bytes = 0;
}

static bool snap_take_cel(Snap* s, const Doc* d, int layerId, int frame)
{
	memset(s, 0, sizeof(*s));
	int li = doc_layer_index(d, layerId);
	if (li < 0 || d->layers[li].isFolder || frame >= d->frameCount) return false;
	size_t n = (size_t)d->w * d->h;
	s->cel = malloc(n);
	s->colors = malloc(sizeof(uint32_t) * (DOC_MAX_COLORS + 1));
	if (!s->cel || !s->colors) { free(s->cel); free(s->colors); s->cel = NULL; s->colors = NULL; return false; }
	memcpy(s->cel, d->layers[li].cels[frame], n);
	memcpy(s->colors, d->colors, sizeof(uint32_t) * (DOC_MAX_COLORS + 1));
	s->colorCount = d->colorCount;
	s->layerId = layerId;
	s->frame = frame;
	s->bytes = n;
	histBytes += n;
	return true;
}

// Put a cel snapshot back. Its pixel values are translated through its own color table, so
// it stays right even if the document's color table was compacted since.
static void snap_restore_cel(Snap* s, Doc* d)
{
	int li = doc_layer_index(d, s->layerId);
	if (li >= 0 && !d->layers[li].isFolder && s->frame < d->frameCount)
	{
		uint8_t map[DOC_MAX_COLORS + 1] = { 0 };
		bool used[DOC_MAX_COLORS + 1] = { false };
		uint8_t* dst = d->layers[li].cels[s->frame];
		size_t n = (size_t)d->w * d->h;
		for (size_t i = 0; i < n; i++) used[s->cel[i]] = true;
		// If the table is too full for a color to come back exactly, drop what this cel shows
		// now (it is about to be replaced), compact, and map again.
		for (int pass = 0; pass < 2; pass++)
		{
			bool exact = true;
			for (int v = 1; v <= s->colorCount; v++)
			{
				if (!used[v]) continue;
				map[v] = doc_color_index(d, s->colors[v], false);
				if (d->colors[map[v]] != (s->colors[v] | 0xFF)) exact = false;
			}
			if (exact || pass) break;
			memset(dst, 0, n);
			doc_compact(d);
		}
		for (size_t i = 0; i < n; i++)
			dst[i] = map[s->cel[i]];
		d->curLayer = li;
		d->curFrame = s->frame;
	}
	snap_free(s);
}

static bool snap_take_meta(Snap* s, const Doc* d, int key)
{
	memset(s, 0, sizeof(*s));
	Meta* m = calloc(1, sizeof(Meta));
	if (!m) return false;
	m->fps = d->fps;
	memcpy(m->frameMs, d->frameMs, sizeof(m->frameMs));
	memcpy(m->tags, d->tags, sizeof(m->tags));
	m->tagCount = d->tagCount;
	memcpy(m->swatches, d->swatches, sizeof(m->swatches));
	m->layerCount = d->layerCount;
	for (int i = 0; i < d->layerCount; i++)
	{
		const Layer* L = &d->layers[i];
		m->layers[i].id = L->id;
		memcpy(m->layers[i].name, L->name, LAYER_NAME_MAX);
		m->layers[i].visible = L->visible;
		m->layers[i].locked = L->locked;
		m->layers[i].collapsed = L->collapsed;
		m->layers[i].opacity = L->opacity;
	}
	s->meta = m;
	s->key = key;
	return true;
}

static void snap_restore_meta(Snap* s, Doc* d)
{
	const Meta* m = s->meta;
	d->fps = m->fps;
	memcpy(d->frameMs, m->frameMs, sizeof(m->frameMs));
	memcpy(d->tags, m->tags, sizeof(m->tags));
	d->tagCount = m->tagCount;
	for (int t = 0; t < d->tagCount; t++)      // stay inside the frames there are now
	{
		if (d->tags[t].to >= d->frameCount) d->tags[t].to = d->frameCount - 1;
		if (d->tags[t].from > d->tags[t].to) d->tags[t].from = d->tags[t].to;
	}
	memcpy(d->swatches, m->swatches, sizeof(m->swatches));
	for (int i = 0; i < m->layerCount; i++)
	{
		int li = doc_layer_index(d, m->layers[i].id);
		if (li < 0) continue;
		Layer* L = &d->layers[li];
		memcpy(L->name, m->layers[i].name, LAYER_NAME_MAX);
		L->visible = m->layers[i].visible;
		L->locked = m->layers[i].locked;
		L->collapsed = m->layers[i].collapsed;
		L->opacity = m->layers[i].opacity;
	}
	snap_free(s);
}

static bool snap_take(Snap* s, const Doc* d)
{
	memset(s, 0, sizeof(*s));
	Doc* c = malloc(sizeof(Doc));
	if (!c) return false;
	*c = *d;
	size_t n = (size_t)d->w * d->h;
	for (int i = 0; i < DOC_MAX_LAYERS; i++)
		memset(c->layers[i].cels, 0, sizeof(c->layers[i].cels));
	for (int i = 0; i < c->layerCount; i++)
		if (!cels_clone(&c->layers[i], &d->layers[i], n))
		{
			doc_free(c);
			free(c);
			return false;
		}
	s->doc = c;
	s->bytes = doc_pixel_bytes(c);
	histBytes += s->bytes;
	return true;
}

// Restore the document from a snapshot (consumes the snapshot's pixel buffers).
static void snap_restore(Snap* s, Doc* d)
{
	if (s->meta) { snap_restore_meta(s, d); return; }
	if (!s->doc) { snap_restore_cel(s, d); return; }
	Doc* c = s->doc;
	char name[DOC_NAME_MAX];
	bool linked = d->linkedSwatches;
	memcpy(name, d->name, sizeof(name));
	doc_free(d);
	*d = *c;
	memcpy(d->name, name, sizeof(name));
	d->linkedSwatches = linked;
	free(c);
	histBytes -= s->bytes;
	s->doc = NULL;
	s->bytes = 0;
}

static void stack_clear(Snap* st, int* n)
{
	for (int i = 0; i < *n; i++)
		snap_free(&st[i]);
	*n = 0;
}

static void drop_oldest_undo(void)
{
	snap_free(&undoStack[0]);
	memmove(&undoStack[0], &undoStack[1], sizeof(Snap) * (undoCount - 1));
	undoCount--;
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
	while (histBytes > HIST_BUDGET && undoCount > 1)
		drop_oldest_undo();
}

void hist_abort(void)
{
	if (hasPending)
		snap_free(&pending);
	hasPending = false;
}

void hist_clear(void)
{
	hist_abort();
	stack_clear(undoStack, &undoCount);
	stack_clear(redoStack, &redoCount);
}

void hist_begin_cel(const Doc* d)
{
	hist_abort();
	hasPending = snap_take_cel(&pending, d, d->layers[d->curLayer].id, d->curFrame);
	if (!hasPending) hasPending = snap_take(&pending, d);
}

void hist_begin(const Doc* d)
{
	hist_abort();
	hasPending = snap_take(&pending, d);
}

void hist_commit(void)
{
	if (!hasPending)
		return;
	stack_clear(redoStack, &redoCount);
	stack_push(undoStack, &undoCount, pending);
	memset(&pending, 0, sizeof(pending));
	hasPending = false;
}

void hist_push(const Doc* d)
{
	hist_begin(d);
	hist_commit();
}

void hist_push_meta(const Doc* d, int key)
{
	if (key && redoCount == 0 && undoCount > 0 && undoStack[undoCount - 1].meta && undoStack[undoCount - 1].key == key)
		return;   // still the same adjustment: the step already holds the value from before it
	Snap s;
	if (!snap_take_meta(&s, d, key)) return;
	stack_clear(redoStack, &redoCount);
	stack_push(undoStack, &undoCount, s);
}

static bool hist_move(Doc* d, Snap* from, int* nFrom, Snap* to, int* nTo)
{
	if (*nFrom == 0)
		return false;
	Snap cur;
	Snap* top = &from[*nFrom - 1];
	if (!(top->meta ? snap_take_meta(&cur, d, 0) : top->doc ? snap_take(&cur, d)
	                : snap_take_cel(&cur, d, top->layerId, top->frame)))
		return false;
	Snap s = from[--(*nFrom)];
	snap_restore(&s, d);
	stack_push(to, nTo, cur);
	d->dirty = true;
	return true;
}

bool hist_undo(Doc* d) { return hist_move(d, undoStack, &undoCount, redoStack, &redoCount); }
bool hist_redo(Doc* d) { return hist_move(d, redoStack, &redoCount, undoStack, &undoCount); }
