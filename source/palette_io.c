// SPDX-License-Identifier: MIT
#include "palette_io.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PALETTE_MAX_BYTES (64 * 1024)

const char* palette_save_hex(const char* path, const uint32_t* colors, int n)
{
	FILE* f = fopen(path, "w");
	if (!f) return "Can't open file";
	for (int i = 0; i < n; i++)
		fprintf(f, "%06lx\n", (unsigned long)(colors[i] >> 8));
	bool ok = !ferror(f);
	ok = (fclose(f) == 0) && ok;
	if (!ok) { remove(path); return "Write failed"; }
	return NULL;
}

const char* palette_save_gpl(const char* path, const char* name, const uint32_t* colors, int n)
{
	FILE* f = fopen(path, "w");
	if (!f) return "Can't open file";
	fprintf(f, "GIMP Palette\nName: %s\nColumns: 16\n#\n", name);
	for (int i = 0; i < n; i++)
	{
		uint32_t c = colors[i];
		fprintf(f, "%3d %3d %3d\t#%06lx\n", (int)(c >> 24) & 255, (int)(c >> 16) & 255, (int)(c >> 8) & 255,
		        (unsigned long)(c >> 8));
	}
	bool ok = !ferror(f);
	ok = (fclose(f) == 0) && ok;
	if (!ok) { remove(path); return "Write failed"; }
	return NULL;
}

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

int palette_parse(const char* text, uint32_t* out, int max)
{
	int n = 0;
	bool gpl = !strncmp(text, "GIMP Palette", 12);
	while (*text && n < max)
	{
		const char* eol = strchr(text, '\n');
		size_t len = eol ? (size_t)(eol - text) : strlen(text);
		const char* p = text;
		const char* end = text + len;
		while (p < end && (*p == ' ' || *p == '\t')) p++;
		if (gpl)
		{
			// "R G B<tab>name"; header lines and comments don't start with a number
			int v[3], k = 0;
			while (k < 3 && p < end && isdigit((unsigned char)*p))
			{
				int x = 0;
				while (p < end && isdigit((unsigned char)*p) && x < 1000) x = x * 10 + (*p++ - '0');
				v[k++] = x;
				while (p < end && (*p == ' ' || *p == '\t')) p++;
			}
			if (k == 3 && v[0] < 256 && v[1] < 256 && v[2] < 256)
				out[n++] = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) | ((uint32_t)v[2] << 8) | 0xFF;
		}
		else
		{
			if (p < end && *p == '#') p++;
			uint32_t c = 0;
			int digits = 0;
			while (p < end && digits < 6 && hex_digit(*p) >= 0) { c = (c << 4) | (uint32_t)hex_digit(*p++); digits++; }
			// exactly six digits, then nothing but an optional alpha pair / spaces
			int extra = 0;
			while (p < end && hex_digit(*p) >= 0) { p++; extra++; }
			while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
			if (digits == 6 && (extra == 0 || extra == 2) && p == end)
				out[n++] = (c << 8) | 0xFF;
		}
		if (!eol) break;
		text = eol + 1;
	}
	return n;
}

int palette_load(const char* path, uint32_t* out, int max)
{
	FILE* f = fopen(path, "rb");
	if (!f) return 0;
	char* buf = malloc(PALETTE_MAX_BYTES + 1);
	if (!buf) { fclose(f); return 0; }
	size_t got = fread(buf, 1, PALETTE_MAX_BYTES, f);
	fclose(f);
	buf[got] = 0;
	int n = palette_parse(buf, out, max);
	free(buf);
	return n;
}
