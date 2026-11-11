// SPDX-License-Identifier: MIT
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS_MAX_BYTES 4096

size_t settings_format(const Setting* items, int n, char* out, size_t cap)
{
	size_t len = 0;
	if (cap) out[0] = 0;
	for (int i = 0; i < n && len + 1 < cap; i++)
	{
		int w = snprintf(out + len, cap - len, "%s=%d\n", items[i].key, *items[i].value);
		if (w < 0 || (size_t)w >= cap - len) { out[len] = 0; break; }   // no room: drop the partial line
		len += (size_t)w;
	}
	return len;
}

void settings_parse(const Setting* items, int n, const char* text)
{
	while (*text)
	{
		const char* eol = strchr(text, '\n');
		size_t lineLen = eol ? (size_t)(eol - text) : strlen(text);
		const char* eq = memchr(text, '=', lineLen);
		if (eq)
		{
			size_t keyLen = (size_t)(eq - text);
			char num[16] = {0};
			size_t numLen = lineLen - keyLen - 1;
			if (numLen > 0 && numLen < sizeof(num))
			{
				memcpy(num, eq + 1, numLen);
				char* end;
				long v = strtol(num, &end, 10);
				while (*end == '\r' || *end == ' ') end++;
				if (end != num && *end == 0)
					for (int i = 0; i < n; i++)
						if (strlen(items[i].key) == keyLen && !memcmp(items[i].key, text, keyLen))
						{
							if (v >= items[i].min && v <= items[i].max) *items[i].value = (int)v;
							break;
						}
			}
		}
		if (!eol) break;
		text = eol + 1;
	}
}

bool settings_load(const char* path, const Setting* items, int n)
{
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	char buf[SETTINGS_MAX_BYTES];
	size_t got = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[got] = 0;
	settings_parse(items, n, buf);
	return true;
}

bool settings_save(const char* path, const Setting* items, int n)
{
	char buf[SETTINGS_MAX_BYTES];
	size_t len = settings_format(items, n, buf, sizeof(buf));
	FILE* f = fopen(path, "wb");
	if (!f) return false;
	bool ok = fwrite(buf, 1, len, f) == len;
	return (fclose(f) == 0) && ok;
}
