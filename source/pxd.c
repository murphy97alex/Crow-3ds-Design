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
	ok = ok && write_chunk(f, "HEAD", head, sizeof(head));

	uint8_t pal[DOC_MAX_COLORS * 4];
	for (int i = 0; i < d->colorCount; i++)
		put32(pal + i * 4, d->colors[i + 1]);
	ok = ok && write_chunk(f, "PALT", pal, d->colorCount * 4);
	uint8_t sw[DOC_PALETTE_SIZE * 4];
	for (int i = 0; i < DOC_PALETTE_SIZE; i++)
		put32(sw + i * 4, d->swatches[i]);
	ok = ok && write_chunk(f, "SWAT", sw, sizeof(sw));

	for (int fr = 0; ok && fr < d->frameCount; fr++)
	{
		// FRAM payload: u16 duration_ms (0 = use fps), u8 encoding (1 = RLE), u8 reserved, data
		buf[0] = 0; buf[1] = 0; buf[2] = 1; buf[3] = 0;
		uint32_t len = rle_encode(d->frames[fr], n, buf + 4);
		ok = write_chunk(f, "FRAM", buf, len + 4);
	}
	ok = ok && write_chunk(f, "END ", NULL, 0);
	ok = (fclose(f) == 0) && ok;
	free(buf);

	if (!ok) { remove(tmp); return "Write failed"; }
	remove(path);
	if (rename(tmp, path) != 0) { remove(tmp); return "Rename failed"; }
	return NULL;
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
	int frames = 0, expectFrames = 0;
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
		else if (!memcmp(id, "FRAM", 4))
		{
			if (!gotHead || frames >= expectFrames) { err = "Bad frame"; break; }
			if (len < 4 || body[2] != 1) { err = "Unknown frame encoding"; break; }
			uint8_t* px = malloc(n);
			if (!px) { err = "Out of memory"; break; }
			nd.frames[frames++] = px;
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
	if (!err && (!gotHead || frames != expectFrames))
		err = "Missing data";
	nd.frameCount = frames;
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
