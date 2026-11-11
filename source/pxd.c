// SPDX-License-Identifier: MIT
#include "pxd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t* p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static uint16_t get16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t get32(const uint8_t* p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }

static bool write_chunk(FILE* f, const char id[4], const uint8_t* data, uint32_t len)
{
	uint8_t hdr[8];
	memcpy(hdr, id, 4);
	put32(hdr + 4, len);
	return fwrite(hdr, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len);
}

// RLE: (count 1..255, value) pairs. Worst case 2 bytes per pixel.
static uint32_t rle_encode(const uint8_t* in, uint32_t n, uint8_t* out)
{
	uint32_t o = 0;
	for (uint32_t i = 0; i < n;)
	{
		uint8_t v = in[i];
		uint32_t run = 1;
		while (i + run < n && in[i + run] == v && run < 255)
			run++;
		out[o++] = run;
		out[o++] = v;
		i += run;
	}
	return o;
}

static bool rle_decode(const uint8_t* in, uint32_t len, uint8_t* out, uint32_t n)
{
	uint32_t o = 0;
	for (uint32_t i = 0; i + 1 < len; i += 2)
	{
		uint32_t run = in[i];
		if (run == 0 || o + run > n)
			return false;
		memset(out + o, in[i + 1], run);
		o += run;
	}
	return o == n && (len % 2) == 0;
}

const char* pxd_save(const Doc* d, const char* path)
{
	uint32_t n = (uint32_t)d->w * d->h;
	uint8_t* buf = malloc(n * 2 + 8);
	if (!buf)
		return "Out of memory";

	char tmp[300];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* f = fopen(tmp, "wb");
	if (!f) { free(buf); return "Can't open file"; }

	bool ok = fwrite("PXD\x1A", 1, 4, f) == 4;

	uint8_t head[16] = {0};
	put16(head + 0, PXD_VERSION);
	put16(head + 2, d->w);
	put16(head + 4, d->h);
	put16(head + 6, d->frameCount);
	put16(head + 8, d->fps);
	put16(head + 10, d->linkedSwatches ? 1 : 0); // flags: bit 0 = linked swatches
	put16(head + 12, d->curLayer);
	ok = ok && write_chunk(f, "HEAD", head, sizeof(head));

	// THMB: u16 w, u16 h, then w*h RGBA pixels (bytes R,G,B,A). Lets the gallery skip the rest.
	{
		static uint32_t th[PXD_THUMB * PXD_THUMB];
		static uint8_t tb[4 + PXD_THUMB * PXD_THUMB * 4];
		int tw, tht;
		pxd_make_thumb(d, th, &tw, &tht);
		put16(tb, tw);
		put16(tb + 2, tht);
		for (int i = 0; i < tw * tht; i++)
		{
			tb[4 + i * 4] = th[i] >> 24;
			tb[5 + i * 4] = th[i] >> 16;
			tb[6 + i * 4] = th[i] >> 8;
			tb[7 + i * 4] = th[i];
		}
		ok = ok && write_chunk(f, "THMB", tb, 4 + tw * tht * 4);
	}

	uint8_t pal[DOC_MAX_COLORS * 4];
	for (int i = 0; i < d->colorCount; i++)
		put32(pal + i * 4, d->colors[i + 1]);
	ok = ok && write_chunk(f, "PALT", pal, d->colorCount * 4);
	uint8_t sw[DOC_PALETTE_SIZE * 4];
	for (int i = 0; i < DOC_PALETTE_SIZE; i++)
		put32(sw + i * 4, d->swatches[i]);
	ok = ok && write_chunk(f, "SWAT", sw, sizeof(sw));

	for (int li = 0; ok && li < d->layerCount; li++)
	{
		const Layer* L = &d->layers[li];
		// LAYR payload: u16 id, u16 folder id + 1 (0 = none), u8 flags, u8 opacity, u16 reserved,
		// name (16 bytes, NUL padded). Its FRAM chunks follow (none for folders).
		uint8_t lh[24] = {0};
		put16(lh, L->id);
		put16(lh + 2, L->folder + 1);
		lh[4] = (L->isFolder ? 1 : 0) | (L->visible ? 2 : 0) | (L->locked ? 4 : 0) | (L->collapsed ? 8 : 0);
		lh[5] = L->opacity;
		memcpy(lh + 8, L->name, strnlen(L->name, LAYER_NAME_MAX));
		ok = write_chunk(f, "LAYR", lh, sizeof(lh));
		for (int fr = 0; ok && !L->isFolder && fr < d->frameCount; fr++)
		{
			// FRAM payload: u16 duration_ms (0 = use fps), u8 encoding, u8 reserved, data.
			// encoding 1 = RLE pixels; 2 = linked cel: u16 index of the earlier frame it shares.
			put16(buf, d->frameMs[fr]);
			buf[3] = 0;
			int g;
			for (g = 0; g < fr && L->cels[g] != L->cels[fr]; g++) {}
			if (g < fr)
			{
				buf[2] = 2;
				put16(buf + 4, g);
				ok = write_chunk(f, "FRAM", buf, 6);
			}
			else
			{
				buf[2] = 1;
				uint32_t len = rle_encode(L->cels[fr], n, buf + 4);
				ok = write_chunk(f, "FRAM", buf, len + 4);
			}
		}
	}
	if (ok && d->tagCount)
	{
		// TAGS: u8 count, then per tag: u16 from, u16 to, u8 direction, name (16 bytes)
		uint8_t tb[1 + DOC_MAX_TAGS * 21] = { 0 };
		tb[0] = d->tagCount;
		for (int t = 0; t < d->tagCount; t++)
		{
			uint8_t* q = tb + 1 + t * 21;
			put16(q, d->tags[t].from);
			put16(q + 2, d->tags[t].to);
			q[4] = d->tags[t].dir;
			memcpy(q + 5, d->tags[t].name, strnlen(d->tags[t].name, TAG_NAME_MAX));
		}
		ok = write_chunk(f, "TAGS", tb, 1 + d->tagCount * 21);
	}
	ok = ok && write_chunk(f, "END ", NULL, 0);
	ok = (fclose(f) == 0) && ok;
	free(buf);

	if (!ok) { remove(tmp); return "Write failed"; }
	// Some file systems (the 3DS SD card) won't rename over an existing file. If the rename
	// still fails, the new data stays in the .tmp file rather than being thrown away.
	if (rename(tmp, path) != 0)
	{
		remove(path);
		if (rename(tmp, path) != 0) return "Rename failed (saved as .pxd.tmp)";
	}
	return NULL;
}

void pxd_make_thumb(const Doc* d, uint32_t* out, int* tw, int* th)
{
	int w = d->w, h = d->h;
	// whole-number enlarge for small canvases, plain shrink for big ones
	int up = PXD_THUMB / (w > h ? w : h);
	if (up >= 1) { *tw = w * up; *th = h * up; }
	else if (w >= h) { *tw = PXD_THUMB; *th = h * PXD_THUMB / w; }
	else { *th = PXD_THUMB; *tw = w * PXD_THUMB / h; }
	if (*tw < 1) *tw = 1;
	if (*th < 1) *th = 1;
	uint32_t* full = malloc((size_t)w * h * 4);
	if (!full) { memset(out, 0, (size_t)*tw * *th * 4); return; }
	doc_composite(d, 0, full);
	for (int y = 0; y < *th; y++)
		for (int x = 0; x < *tw; x++)
			out[y * *tw + x] = full[(y * h / *th) * w + x * w / *tw];
	free(full);
}

bool pxd_read_info(const char* path, int* w, int* h, int* layers, int* frames, bool* hasThumb,
                   uint32_t* thumb, int* tw, int* th)
{
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	uint8_t hdr[8];
	bool gotHead = false;
	*hasThumb = false;
	*layers = 0;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fread(hdr, 1, 4, f) != 4 || memcmp(hdr, "PXD\x1A", 4)) { fclose(f); return false; }
	while (fread(hdr, 1, 8, f) == 8)
	{
		uint32_t len = get32(hdr + 4);
		long at = ftell(f);
		if (at < 0 || size < at || len > (uint32_t)(size - at)) break;   // chunk runs past the end
		if (!memcmp(hdr, "HEAD", 4) && len >= 12 && len <= 64)
		{
			uint8_t b[64];
			if (fread(b, 1, len, f) != len) break;
			*w = get16(b + 2);
			*h = get16(b + 4);
			*frames = get16(b + 6);
			if (*frames < 1) *frames = 1;
			gotHead = true;
			continue;
		}
		if (!memcmp(hdr, "THMB", 4) && len >= 4 && len <= 4 + PXD_THUMB * PXD_THUMB * 4)
		{
			uint8_t b[4];
			if (fread(b, 1, 4, f) != 4) break;
			*tw = get16(b);
			*th = get16(b + 2);
			if (*tw < 1 || *th < 1 || *tw > PXD_THUMB || *th > PXD_THUMB || (uint32_t)(4 + *tw * *th * 4) != len) break;
			int got = 0;
			for (; got < *tw * *th; got++)
			{
				uint8_t p[4];
				if (fread(p, 1, 4, f) != 4) break;
				thumb[got] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
			}
			if (got != *tw * *th) break;
			*hasThumb = true;
			continue;
		}
		if (!memcmp(hdr, "LAYR", 4)) (*layers)++;
		if (!memcmp(hdr, "FRAM", 4) && *layers == 0) *layers = 1;   // pre-layer files
		if (!memcmp(hdr, "END ", 4)) break;
		if (fseek(f, (long)len, SEEK_CUR)) break;
	}
	fclose(f);
	return gotHead;
}

// Names from a file end up on screen, in export file names and in JSON: keep plain printable
// ASCII and nothing a path could trip over.
static void clean_name(char* s, size_t n)
{
	s[n - 1] = 0;
	for (; *s; s++)
		if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E || strchr("/\\:*?\"<>|", *s)) *s = '_';
}

// Durations are stored with every layer's frames; read them from the first drawable layer.
static int first_drawable(const Doc* d)
{
	for (int i = 0; i < d->layerCount; i++)
		if (!d->layers[i].isFolder) return i;
	return -1;
}
#define firstDrawable(nd) first_drawable(&(nd))

const char* pxd_load(Doc* d, const char* path)
{
	FILE* f = fopen(path, "rb");
	if (!f)
		return "Can't open file";
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size < 12 || size > 16 * 1024 * 1024) { fclose(f); return "Bad file size"; }
	uint8_t* data = malloc(size);
	if (!data) { fclose(f); return "Out of memory"; }
	bool rd = fread(data, 1, size, f) == (size_t)size;
	fclose(f);
	const char* err = rd ? pxd_load_mem(d, data, (size_t)size) : "Not a .pxd file";
	free(data);
	return err;
}

const char* pxd_load_mem(Doc* d, const uint8_t* data, size_t dataSize)
{
	if (dataSize < 12 || dataSize > 16 * 1024 * 1024) return "Bad file size";
	long size = (long)dataSize;
	if (memcmp(data, "PXD\x1A", 4) != 0) return "Not a .pxd file";

	Doc nd = {0};
	const char* err = NULL;
	bool gotHead = false, gotEnd = false, gotSwatches = false;
	int frames = 0, expectFrames = 0, curL = -1, headCur = -1;
	uint32_t n = 0;

	for (long p = 4; p + 8 <= size && !err && !gotEnd;)
	{
		const uint8_t* id = data + p;
		uint32_t len = get32(data + p + 4);
		const uint8_t* body = data + p + 8;
		if ((uint64_t)p + 8 + len > (uint64_t)size) { err = "Truncated file"; break; }

		if (!memcmp(id, "HEAD", 4))
		{
			if (len < 12 || gotHead) { err = "Bad header"; break; }   // one HEAD: sizes can't change later
			if (get16(body) > PXD_VERSION) { err = "File is from a newer version"; break; }
			nd.w = get16(body + 2);
			nd.h = get16(body + 4);
			expectFrames = get16(body + 6);
			nd.fps = get16(body + 8);
			if (nd.fps < 1) nd.fps = 8;
			if (nd.fps > 100) nd.fps = 100;
			nd.linkedSwatches = get16(body + 10) & 1;
			headCur = len >= 14 ? get16(body + 12) : -1;
			if (nd.w < 1 || nd.h < 1 || nd.w > DOC_MAX_W || nd.h > DOC_MAX_H ||
			    expectFrames < 1 || expectFrames > DOC_MAX_FRAMES)
				{ err = "Unsupported size"; break; }
			n = (uint32_t)nd.w * nd.h;
			memcpy(nd.swatches, DOC_DEFAULT_PALETTE, sizeof(nd.swatches));
			gotHead = true;
		}
		else if (!memcmp(id, "PALT", 4))
		{
			nd.colorCount = 0;
			for (uint32_t i = 0; i < len / 4 && i < DOC_MAX_COLORS; i++)
				nd.colors[++nd.colorCount] = get32(body + i * 4);
		}
		else if (!memcmp(id, "SWAT", 4))
		{
			for (uint32_t i = 0; i < len / 4 && i < DOC_PALETTE_SIZE; i++)
				nd.swatches[i] = get32(body + i * 4);
			gotSwatches = true;
		}
		else if (!memcmp(id, "LAYR", 4))
		{
			if (!gotHead || len < 24 || nd.layerCount >= DOC_MAX_LAYERS) { err = "Bad layer"; break; }
			if (curL >= 0 && !nd.layers[curL].isFolder && frames != expectFrames) { err = "Missing frames"; break; }
			Layer* L = &nd.layers[nd.layerCount];
			memset(L, 0, sizeof(*L));
			L->id = get16(body);
			if (doc_layer_index(&nd, L->id) >= 0) { err = "Bad layer"; break; }   // ids must be unique
			L->folder = (int)get16(body + 2) - 1;
			L->isFolder = body[4] & 1;
			L->visible = body[4] & 2;
			L->locked = body[4] & 4;
			L->collapsed = body[4] & 8;
			L->opacity = body[5];
			memcpy(L->name, body + 8, LAYER_NAME_MAX - 1);
			clean_name(L->name, LAYER_NAME_MAX);
			curL = nd.layerCount++;
			frames = 0;
		}
		else if (!memcmp(id, "FRAM", 4))
		{
			if (!gotHead) { err = "Bad frame"; break; }
			if (curL < 0)
			{
				// version 1/2 file: one implicit layer
				Layer* L = &nd.layers[0];
				memset(L, 0, sizeof(*L));
				L->id = 1;
				L->folder = -1;
				L->visible = true;
				L->opacity = 255;
				snprintf(L->name, sizeof(L->name), "Layer 1");
				nd.layerCount = 1;
				curL = 0;
				frames = 0;
			}
			if (nd.layers[curL].isFolder || frames >= expectFrames) { err = "Bad frame"; break; }
			if (len < 4 || (body[2] != 1 && body[2] != 2)) { err = "Unknown frame encoding"; break; }
			if (curL == firstDrawable(nd)) nd.frameMs[frames] = get16(body);
			if (body[2] == 2)
			{
				int g = len >= 6 ? get16(body + 4) : -1;
				if (g < 0 || g >= frames) { err = "Bad linked frame"; break; }
				nd.layers[curL].cels[frames] = nd.layers[curL].cels[g];
				frames++;
				p += 8 + len;
				continue;
			}
			uint8_t* px = malloc(n);
			if (!px) { err = "Out of memory"; break; }
			nd.layers[curL].cels[frames++] = px;
			if (!rle_decode(body + 4, len - 4, px, n)) { err = "Corrupt frame"; break; }
			for (uint32_t i = 0; i < n; i++)
				if (px[i] > nd.colorCount) px[i] = PIX_TRANSPARENT;
		}
		else if (!memcmp(id, "TAGS", 4) && len >= 1)
		{
			int cnt = body[0];
			for (int t = 0; t < cnt && nd.tagCount < DOC_MAX_TAGS && 1 + (uint32_t)(t + 1) * 21 <= len; t++)
			{
				const uint8_t* q = body + 1 + t * 21;
				Tag* T = &nd.tags[nd.tagCount++];
				int from = get16(q), to = get16(q + 2);    // clamped to the frame count after loading
				T->from = from < DOC_MAX_FRAMES ? from : DOC_MAX_FRAMES - 1;
				T->to = to < DOC_MAX_FRAMES ? to : DOC_MAX_FRAMES - 1;
				T->dir = q[4] < TAG_DIR_COUNT ? q[4] : TAG_FORWARD;
				memcpy(T->name, q + 5, TAG_NAME_MAX - 1);
				clean_name(T->name, TAG_NAME_MAX);
			}
		}
		else if (!memcmp(id, "END ", 4))
			gotEnd = true;
		// unknown chunks are skipped
		p += 8 + len;
	}

	// version 1 files: the palette was both the color table and the swatches
	if (!gotSwatches)
		for (int i = 0; i < DOC_PALETTE_SIZE && i < nd.colorCount; i++)
			nd.swatches[i] = nd.colors[i + 1];
	nd.frameCount = expectFrames;
	if (!err && (!gotHead || nd.layerCount == 0))
		err = "Missing data";
	int drawable = 0;
	for (int i = 0; !err && i < nd.layerCount; i++)
	{
		Layer* L = &nd.layers[i];
		if (L->isFolder) continue;
		drawable++;
		for (int fr = 0; fr < expectFrames; fr++)
			if (!L->cels[fr]) { err = "Missing frames"; break; }
	}
	if (!err && drawable == 0)
		err = "No layers";
	for (int t = 0; !err && t < nd.tagCount; t++)
	{
		Tag* T = &nd.tags[t];
		if (T->to >= expectFrames) T->to = expectFrames - 1;
		if (T->from > T->to) T->from = T->to;
	}
	if (!err)
	{
		nd.nextLayerId = 1;
		for (int i = 0; i < nd.layerCount; i++)
			if (nd.layers[i].id >= nd.nextLayerId) nd.nextLayerId = nd.layers[i].id + 1;
		// A member must sit below its folder with only fellow members in between (top down, so
		// a member that loses its folder also frees the ones below it).
		for (int i = nd.layerCount - 1; i >= 0; i--)
		{
			Layer* L = &nd.layers[i];
			if (L->folder < 0) continue;
			int f = doc_layer_index(&nd, L->folder);
			bool ok = !L->isFolder && f > i && nd.layers[f].isFolder;
			for (int j = i + 1; ok && j < f; j++)
				ok = nd.layers[j].folder == L->folder;
			if (!ok) L->folder = -1;
		}
		nd.curLayer = -1;
		if (headCur >= 0 && headCur < nd.layerCount && !nd.layers[headCur].isFolder)
			nd.curLayer = headCur;
		for (int i = nd.layerCount - 1; nd.curLayer < 0 && i >= 0; i--)
			if (!nd.layers[i].isFolder) nd.curLayer = i;
	}
	if (err)
	{
		doc_free(&nd);
		return err;
	}
	doc_free(d);
	*d = nd;
	d->curFrame = 0;
	d->dirty = false;
	return NULL;
}
