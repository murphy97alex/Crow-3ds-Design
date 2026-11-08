// SPDX-License-Identifier: MIT
#include "png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void be32(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static bool chunk(FILE* f, const char* type, const uint8_t* data, uint32_t len)
{
	uint8_t hdr[8];
	be32(hdr, len);
	memcpy(hdr + 4, type, 4);
	uint32_t crc = crc32(0, hdr + 4, 4);
	if (len) crc = crc32(crc, data, len);
	uint8_t c[4];
	be32(c, crc);
	return fwrite(hdr, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) && fwrite(c, 1, 4, f) == 4;
}

const char* png_export(const Doc* d, int frame, int outW, int outH, const char* path)
{
	if (outW < 1 || outH < 1 || outW > PNG_MAX_DIM || outH > PNG_MAX_DIM)
		return "Bad size";
	const uint8_t* src = d->frames[frame];
	size_t rowLen = (size_t)outW * 4 + 1;
	uint8_t* row = malloc(rowLen);
	enum { OUT_CHUNK = 64 * 1024 };
	uint8_t* out = malloc(OUT_CHUNK);
	if (!row || !out) { free(row); free(out); return "Out of memory"; }

	FILE* f = fopen(path, "wb");
	if (!f) { free(row); free(out); return "Can't open file"; }

	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	uint8_t ihdr[13];
	be32(ihdr, outW);
	be32(ihdr + 4, outH);
	ihdr[8] = 8;  // bit depth
	ihdr[9] = 6;  // RGBA
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	bool ok = fwrite(sig, 1, 8, f) == 8 && chunk(f, "IHDR", ihdr, 13);

	z_stream z;
	memset(&z, 0, sizeof(z));
	ok = ok && deflateInit(&z, 6) == Z_OK;
	bool zInit = ok;

	for (int y = 0; ok && y <= outH; y++)
	{
		bool last = y == outH;
		if (!last)
		{
			int sy = (int)((long)y * d->h / outH);
			row[0] = 0; // filter: none
			for (int x = 0; x < outW; x++)
			{
				int sx = (int)((long)x * d->w / outW);
				uint8_t v = src[sy * d->w + sx];
				uint32_t c = v == PIX_TRANSPARENT ? 0 : d->palette[v - 1];
				uint8_t* p = row + 1 + x * 4;
				p[0] = c >> 24; p[1] = c >> 16; p[2] = c >> 8; p[3] = c;
			}
			z.next_in = row;
			z.avail_in = rowLen;
		}
		int flush = last ? Z_FINISH : Z_NO_FLUSH;
		int r;
		do
		{
			z.next_out = out;
			z.avail_out = OUT_CHUNK;
			r = deflate(&z, flush);
			if (r == Z_STREAM_ERROR) { ok = false; break; }
			uint32_t have = OUT_CHUNK - z.avail_out;
			if (have && !chunk(f, "IDAT", out, have)) { ok = false; break; }
		} while (z.avail_out == 0 || (last && r != Z_STREAM_END));
	}
	if (zInit) deflateEnd(&z);
	ok = ok && chunk(f, "IEND", NULL, 0);
	ok = (fclose(f) == 0) && ok;
	free(row);
	free(out);
	if (!ok) { remove(path); return "Write failed"; }
	return NULL;
}
