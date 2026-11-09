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
			// FRAM payload: u16 duration_ms (0 = use fps), u8 encoding (1 = RLE), u8 reserved, data
			buf[0] = 0; buf[1] = 0; buf[2] = 1; buf[3] = 0;
			uint32_t len = rle_encode(L->cels[fr], n, buf + 4);
			ok = write_chunk(f, "FRAM", buf, len + 4);
		}
	}
	ok = ok && write_chunk(f, "END ", NULL, 0);
	ok = (fclose(f) == 0) && ok;
	free(buf);

	if (!ok) { remove(tmp); return "Write failed"; }
	remove(path);
	if (rename(tmp, path) != 0) { remove(tmp); return "Rename failed"; }
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

bool pxd_read_info(const char* path, int* w, int* h, int* layers, bool* hasThumb,
                   uint32_t* thumb, int* tw, int* th)
{
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	uint8_t hdr[8];
	bool gotHead = false;
	*hasThumb = false;
	*layers = 0;
	if (fread(hdr, 1, 4, f) != 4 || memcmp(hdr, "PXD\x1A", 4)) { fclose(f); return false; }
	while (fread(hdr, 1, 8, f) == 8)
	{
		uint32_t len = get32(hdr + 4);
		if (!memcmp(hdr, "HEAD", 4) && len >= 12 && len <= 64)
		{
			uint8_t b[64];
			if (fread(b, 1, len, f) != len) break;
			*w = get16(b + 2);
			*h = get16(b + 4);
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
			for (int i = 0; i < *tw * *th; i++)
			{
				uint8_t p[4];
				if (fread(p, 1, 4, f) != 4) break;
				thumb[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
			}
			*hasThumb = true;
			continue;
		}
		if (!memcmp(hdr, "LAYR", 4)) (*layers)++;
		if (!memcmp(hdr, "FRAM", 4) && *layers == 0) *layers = 1;   // pre-layer files
		if (!memcmp(hdr, "END ", 4)) break;
		if (fseek(f, len, SEEK_CUR)) break;
	}
	fclose(f);
	return gotHead;
}

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
	if (!rd || memcmp(data, "PXD\x1A", 4) != 0) { free(data); return "Not a .pxd file"; }

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
			if (len < 12) { err = "Bad header"; break; }
			if (get16(body) > PXD_VERSION) { err = "File is from a newer version"; break; }
			nd.w = get16(body + 2);
			nd.h = get16(body + 4);
			expectFrames = get16(body + 6);
			nd.fps = get16(body + 8);
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
			L->folder = (int)get16(body + 2) - 1;
			L->isFolder = body[4] & 1;
			L->visible = body[4] & 2;
			L->locked = body[4] & 4;
			L->collapsed = body[4] & 8;
			L->opacity = body[5];
			memcpy(L->name, body + 8, LAYER_NAME_MAX - 1);
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
			if (len < 4 || body[2] != 1) { err = "Unknown frame encoding"; break; }
			uint8_t* px = malloc(n);
			if (!px) { err = "Out of memory"; break; }
			nd.layers[curL].cels[frames++] = px;
			if (!rle_decode(body + 4, len - 4, px, n)) { err = "Corrupt frame"; break; }
			for (uint32_t i = 0; i < n; i++)
				if (px[i] > nd.colorCount) px[i] = PIX_TRANSPARENT;
		}
		else if (!memcmp(id, "END ", 4))
			gotEnd = true;
		// unknown chunks are skipped
		p += 8 + len;
	}
	free(data);

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
	if (!err)
	{
		nd.nextLayerId = 1;
		for (int i = 0; i < nd.layerCount; i++)
			if (nd.layers[i].id >= nd.nextLayerId) nd.nextLayerId = nd.layers[i].id + 1;
		for (int i = 0; i < nd.layerCount; i++)
			if (nd.layers[i].folder >= 0 && (nd.layers[i].isFolder || doc_layer_index(&nd, nd.layers[i].folder) < 0))
				nd.layers[i].folder = -1;
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
