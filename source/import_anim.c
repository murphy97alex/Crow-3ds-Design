// Import animations: sprite sheets and GIFs become multi-frame projects.
// SPDX-License-Identifier: MIT
#include "import.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Convert one frame's RGBA (rows `stride` pixels apart) into d's color table.
static void fill_cel(Doc* d, uint8_t* cel, const uint32_t* src, int stride)
{
	uint32_t lastC = 0;
	uint8_t lastV = 0;
	for (int y = 0; y < d->h; y++)
		for (int x = 0; x < d->w; x++)
		{
			uint32_t c = src[(size_t)y * stride + x];
			if ((c & 255) < 128) { cel[y * d->w + x] = PIX_TRANSPARENT; continue; }
			c |= 0xFF;
			if (c != lastC || !lastV) { lastV = doc_color_index(d, c, false); lastC = c; }
			cel[y * d->w + x] = lastV;
		}
}

// Hand a finished temporary document over to d.
static void adopt(Doc* d, Doc* nd)
{
	doc_free(d);
	*d = *nd;
	d->curFrame = 0;
	d->dirty = true;
}

const char* import_sheet_doc(Doc* d, const uint32_t* rgba, int w, int h, int cols, int rows)
{
	if (cols < 1 || rows < 1 || cols > w || rows > h) return "Bad frame count";
	if (cols * rows > DOC_MAX_FRAMES) return "Too many frames (64 max)";
	int fw = w / cols, fh = h / rows;
	if (fw > DOC_MAX_W || fh > DOC_MAX_H) return "Frames are bigger than 320x240";
	// drop empty cells at the end (sheets are often padded out to a full grid)
	int n = cols * rows;
	for (; n > 1; n--)
	{
		int cx = ((n - 1) % cols) * fw, cy = ((n - 1) / cols) * fh;
		bool empty = true;
		for (int y = 0; y < fh && empty; y++)
			for (int x = 0; x < fw && empty; x++)
				empty = (rgba[(size_t)(cy + y) * w + cx + x] & 255) < 128;
		if (!empty) break;
	}
	Doc nd = {0};
	if (!doc_new(&nd, fw, fh)) return "Out of memory";
	for (int k = 0; k < n; k++)
	{
		if (k > 0 && !doc_frame_insert(&nd, k, -1)) { doc_free(&nd); return "Out of memory"; }
		fill_cel(&nd, nd.layers[0].cels[k], rgba + (size_t)((k / cols) * fh) * w + (k % cols) * fw, w);
	}
	adopt(d, &nd);
	return NULL;
}

// ---- GIF ---------------------------------------------------------------------------

#define GIF_MAX_BYTES (8 * 1024 * 1024)
#define GIF_MAX_CANVAS 2048        // per side; the canvas is held as RGBA while decoding

// Standard GIF LZW. Fills out[0..outLen); stops quietly at the end of the data or the output.
// Returns false only for data that can't be LZW at all.
static bool lzw_decode(const uint8_t* in, size_t inLen, int minCode, uint8_t* out, size_t outLen)
{
	if (minCode < 2 || minCode > 8) return false;
	static uint16_t prefix[4096];
	static uint8_t suffix[4096], stack[4098];
	int clear = 1 << minCode, eoi = clear + 1, next = eoi + 1, size = minCode + 1, prev = -1;
	uint32_t bits = 0;
	int nb = 0;
	size_t ip = 0, op = 0;
	uint8_t first = 0;
	while (op < outLen)
	{
		while (nb < size)
		{
			if (ip >= inLen) return true;
			bits |= (uint32_t)in[ip++] << nb;
			nb += 8;
		}
		int code = (int)(bits & ((1u << size) - 1));
		bits >>= size;
		nb -= size;
		if (code == clear) { next = eoi + 1; size = minCode + 1; prev = -1; continue; }
		if (code == eoi) break;
		int sp = 0, c = code;
		if (prev < 0) { if (code >= clear) return false; }
		else if (code >= next)
		{
			if (code > next) return false;
			stack[sp++] = first;         // the string that is being defined right now
			c = prev;
		}
		while (c >= clear)
		{
			if (c >= next || c <= eoi || sp >= 4096) return false;
			stack[sp++] = suffix[c];
			c = prefix[c];
		}
		stack[sp++] = (uint8_t)c;
		first = (uint8_t)c;
		while (sp > 0 && op < outLen) out[op++] = stack[--sp];
		if (prev >= 0 && next < 4096)
		{
			prefix[next] = (uint16_t)prev;
			suffix[next] = first;
			next++;
			if (next == (1 << size) && size < 12) size++;
		}
		prev = code;
	}
	return true;
}

const char* import_gif_doc(Doc* d, const char* path, bool* truncated)
{
	FILE* f = fopen(path, "rb");
	if (!f) return "Can't open file";
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size < 13 || size > GIF_MAX_BYTES) { fclose(f); return "GIF too large (8 MB max)"; }
	uint8_t* data = malloc((size_t)size);
	if (!data) { fclose(f); return "Out of memory"; }
	bool rd = fread(data, 1, (size_t)size, f) == (size_t)size;
	fclose(f);
	const char* err = rd ? import_gif_mem(d, data, (size_t)size, truncated) : "Couldn't read file";
	free(data);
	return err;
}

const char* import_gif_mem(Doc* d, const uint8_t* b, size_t size, bool* truncated)
{
	if (truncated) *truncated = false;
	if (size < 13 || (memcmp(b, "GIF87a", 6) && memcmp(b, "GIF89a", 6))) return "Not a GIF";
	int W = b[6] | (b[7] << 8), H = b[8] | (b[9] << 8);
	if (W < 1 || H < 1 || W > GIF_MAX_CANVAS || H > GIF_MAX_CANVAS) return "GIF too large (2048 px max)";
	size_t p = 13;
	const uint8_t* gct = NULL;
	int gctN = 0;
	if (b[10] & 0x80)
	{
		gctN = 2 << (b[10] & 7);
		if (p + (size_t)gctN * 3 > size) return "Corrupt GIF";
		gct = b + p;
		p += (size_t)gctN * 3;
	}
	// output size: shrunk (nearest) to fit a canvas
	int ow = W, oh = H;
	if (ow > DOC_MAX_W || oh > DOC_MAX_H)
	{
		float s = (float)DOC_MAX_W / W;
		if ((float)DOC_MAX_H / H < s) s = (float)DOC_MAX_H / H;
		ow = (int)(W * s);
		oh = (int)(H * s);
		if (ow < 1) ow = 1;
		if (oh < 1) oh = 1;
	}

	size_t n = (size_t)W * H;
	uint32_t* canvas = calloc(n, 4);          // what the viewer would show (0 = transparent)
	uint32_t* saved = NULL;                   // copy for "restore to previous" frames
	uint8_t* idx = malloc(n);
	uint8_t* lzw = malloc(size);              // an image's data can't be longer than the file
	uint32_t* small = ow != W || oh != H ? malloc((size_t)ow * oh * 4) : NULL;
	Doc nd = {0};
	const char* err = NULL;
	if (!canvas || !idx || !lzw || ((ow != W || oh != H) && !small)) err = "Out of memory";
	else if (!doc_new(&nd, ow, oh)) err = "Out of memory";

	int frames = 0, delay = 0, transIdx = -1, disposal = 0;
	while (!err && p < size)
	{
		uint8_t t = b[p++];
		if (t == 0x3B) break;
		if (t == 0x21)
		{
			if (p >= size) break;
			uint8_t label = b[p++];
			if (label == 0xF9 && p + 5 <= size && b[p] == 4)
			{
				disposal = (b[p + 1] >> 2) & 7;
				delay = b[p + 2] | (b[p + 3] << 8);
				transIdx = (b[p + 1] & 1) ? b[p + 4] : -1;
			}
			while (p < size && b[p]) p += (size_t)b[p] + 1;   // skip sub-blocks
			p++;
			continue;
		}
		if (t != 0x2C) break;                 // not a block we know: stop at what we have
		if (p + 9 > size) break;
		int ix = b[p] | (b[p + 1] << 8), iy = b[p + 2] | (b[p + 3] << 8);
		int iw = b[p + 4] | (b[p + 5] << 8), ih = b[p + 6] | (b[p + 7] << 8);
		uint8_t flags = b[p + 8];
		p += 9;
		const uint8_t* pal = gct;
		int palN = gctN;
		if (flags & 0x80)
		{
			palN = 2 << (flags & 7);
			if (p + (size_t)palN * 3 > size) break;
			pal = b + p;
			p += (size_t)palN * 3;
		}
		if (p >= size) break;
		int minCode = b[p++];
		size_t len = 0;
		while (p < size && b[p])
		{
			size_t blk = b[p];
			if (p + 1 + blk > size) blk = size - p - 1;
			memcpy(lzw + len, b + p + 1, blk);
			len += blk;
			p += blk + 1;
		}
		p++;
		if (iw < 1 || ih < 1 || ix + iw > W || iy + ih > H || !pal) { err = frames ? NULL : "Corrupt GIF"; break; }
		if (frames >= DOC_MAX_FRAMES) { if (truncated) *truncated = true; break; }

		size_t in = (size_t)iw * ih;
		memset(idx, 0, in);
		if (!lzw_decode(lzw, len, minCode, idx, in)) { err = frames ? NULL : "Corrupt GIF"; break; }
		if (disposal == 3)
		{
			if (!saved) saved = malloc(n * 4);
			if (saved) memcpy(saved, canvas, n * 4);
		}
		// draw the image onto the canvas (interlaced images store their rows in four passes)
		static const int startRow[4] = { 0, 4, 2, 1 }, stepRow[4] = { 8, 8, 4, 2 };
		int srcRow = 0;
		for (int pass = 0; pass < ((flags & 0x40) ? 4 : 1); pass++)
			for (int y = (flags & 0x40) ? startRow[pass] : 0; y < ih; y += (flags & 0x40) ? stepRow[pass] : 1, srcRow++)
			{
				const uint8_t* row = idx + (size_t)srcRow * iw;
				uint32_t* dst = canvas + (size_t)(iy + y) * W + ix;
				for (int x = 0; x < iw; x++)
				{
					int v = row[x];
					if (v == transIdx || v >= palN) continue;
					dst[x] = ((uint32_t)pal[v * 3] << 24) | ((uint32_t)pal[v * 3 + 1] << 16) | ((uint32_t)pal[v * 3 + 2] << 8) | 0xFF;
				}
			}

		// store the frame
		if (frames > 0 && !doc_frame_insert(&nd, frames, -1)) { err = "Out of memory"; break; }
		const uint32_t* src = canvas;
		if (small)
		{
			for (int y = 0; y < oh; y++)
				for (int x = 0; x < ow; x++)
					small[y * ow + x] = canvas[(size_t)(y * (long)H / oh) * W + (x * (long)W / ow)];
			src = small;
		}
		fill_cel(&nd, nd.layers[0].cels[frames], src, ow);
		int ms = delay * 10;
		if (ms < 20) ms = 100;                // browsers treat 0-1 cs as "no delay given"
		nd.frameMs[frames] = ms > 9990 ? 9990 : ms;
		frames++;

		// get the canvas ready for the next frame
		if (disposal == 2)
			for (int y = 0; y < ih; y++)
				memset(canvas + (size_t)(iy + y) * W + ix, 0, (size_t)iw * 4);
		else if (disposal == 3 && saved)
			memcpy(canvas, saved, n * 4);
		delay = 0;
		transIdx = -1;
		disposal = 0;
	}
	if (!err && frames == 0) err = "No pictures in this GIF";
	free(canvas);
	free(saved);
	free(idx);
	free(lzw);
	free(small);
	if (err) { doc_free(&nd); return err; }
	adopt(d, &nd);
	return NULL;
}
