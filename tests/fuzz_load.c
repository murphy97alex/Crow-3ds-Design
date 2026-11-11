// Fuzz test for the two loaders that read files from the SD card as untrusted input:
// .pxd projects and GIFs. Run under AddressSanitizer / UBSan: any crash or bad read fails.
//
// Two ways to use it:
//  - As a normal host test (the default): starts from valid files made on the spot and tries
//    40,000 random corruptions of each. `fuzz_load 500000` runs more rounds.
//  - With libFuzzer for a longer hunt:
//      clang -g -fsanitize=fuzzer,address,undefined -DFUZZ_LIBFUZZER -I source tests/fuzz_load.c
//        source/pxd.c source/doc.c source/anim.c source/export_png.c source/import_anim.c -lz -lm -o fuzz
//      ./fuzz -max_len=4096      (the clang command is one line)
// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "anim.h"
#include "doc.h"
#include "import.h"
#include "pxd.h"

static void use_doc(Doc* d)
{
	// touch what the app would touch right after opening
	static uint32_t out[DOC_MAX_W * DOC_MAX_H];
	assert(d->w >= 1 && d->w <= DOC_MAX_W && d->h >= 1 && d->h <= DOC_MAX_H);
	assert(d->frameCount >= 1 && d->frameCount <= DOC_MAX_FRAMES && d->layerCount >= 1 && d->layerCount <= DOC_MAX_LAYERS);
	assert(d->tagCount >= 0 && d->tagCount <= DOC_MAX_TAGS && !d->layers[d->curLayer].isFolder);
	for (int f = 0; f < d->frameCount; f++)
	{
		doc_composite(d, f, out);
		assert(doc_frame_ms(d, f) >= 10);
	}
	for (int t = 0; t < d->tagCount; t++)
		assert(d->tags[t].from <= d->tags[t].to && d->tags[t].to < d->frameCount);
	int seq[ANIM_MAX_SEQ];
	for (int t = -1; t < d->tagCount; t++)
		assert(anim_sequence(d, t, seq) >= 1);
	for (int i = 0; i < d->layerCount; i++)
	{
		doc_layer_move(d, i, +1);
		doc_layer_move(d, i, -1);
	}
	doc_frame_delete(d, 0);
	doc_compact(d);
	doc_free(d);
}

static void fuzz_one(const uint8_t* data, size_t size)
{
	static Doc d;
	bool cut;
	if (!pxd_load_mem(&d, data, size)) use_doc(&d);
	if (!import_gif_mem(&d, data, size, &cut)) use_doc(&d);
}

#ifdef FUZZ_LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	fuzz_one(data, size);
	return 0;
}
#else
static uint32_t rng = 0x2545F491u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static size_t read_file(const char* path, uint8_t* buf, size_t cap)
{
	FILE* f = fopen(path, "rb");
	assert(f);
	size_t n = fread(buf, 1, cap, f);
	fclose(f);
	return n;
}

static void mutate_and_run(const uint8_t* seed, size_t n, int rounds)
{
	static uint8_t buf[8192];
	assert(n > 16 && n <= sizeof(buf) / 2);
	for (int r = 0; r < rounds; r++)
	{
		size_t len = n;
		memcpy(buf, seed, n);
		int edits = 1 + rnd() % 4;
		for (int e = 0; e < edits; e++)
		{
			size_t at = rnd() % len;
			switch (rnd() % 6)
			{
			case 0: buf[at] = (uint8_t)rnd(); break;                               // random byte
			case 1: buf[at] ^= (uint8_t)(1u << (rnd() % 8)); break;                // bit flip
			case 2: buf[at] = (rnd() & 1) ? 0xFF : 0; break;                       // extremes
			case 3: len = 1 + rnd() % len; break;                                  // cut short
			case 4:                                                                // repeat a piece
			{
				size_t from = rnd() % len, cnt = 1 + rnd() % 64;
				if (from + cnt > len) cnt = len - from;
				if (len + cnt <= sizeof(buf)) { memmove(buf + at + cnt, buf + at, len - at); memmove(buf + at, buf + from + (from >= at ? cnt : 0), cnt); len += cnt; }
				break;
			}
			default:                                                               // a big little-endian number
				if (at + 4 <= len) { uint32_t v = rnd() % 3 ? 0xFFFFFFF0u + rnd() % 16 : rnd(); memcpy(buf + at, &v, 4); }
				break;
			}
		}
		fuzz_one(buf, len);
	}
}

int main(int argc, char** argv)
{
	int rounds = argc > 1 ? atoi(argv[1]) : 40000;
	// seeds: a project with layers, a folder, linked cels and tags; and a GIF of it
	static Doc s;
	assert(doc_new(&s, 9, 7));
	for (int i = 0; i < 9 * 7; i++) doc_pixels(&s)[i] = (i % 5) ? doc_color_index(&s, 0x102030FF + ((uint32_t)(i % 7) << 20), true) : 0;
	assert(doc_layer_add(&s, true) >= 0 && doc_layer_add(&s, false) >= 0);
	doc_layer_move(&s, s.curLayer, -1);
	assert(doc_frame_insert(&s, 1, 0) && doc_frame_insert(&s, 2, -1) && doc_cel_link_prev(&s, 0, 2));
	s.tagCount = 2;
	s.tags[0] = (Tag){ "walk", 0, 1, TAG_PINGPONG };
	s.tags[1] = (Tag){ "idle", 2, 2, TAG_REVERSE };
	s.frameMs[1] = 250;
	assert(!pxd_save(&s, "/tmp/pc_fuzz.pxd") && !gif_export(&s, -1, 2, "/tmp/pc_fuzz.gif"));
	doc_free(&s);
	hist_clear();

	static uint8_t seed[4096];
	size_t n = read_file("/tmp/pc_fuzz.pxd", seed, sizeof(seed));
	fuzz_one(seed, n);
	mutate_and_run(seed, n, rounds);
	n = read_file("/tmp/pc_fuzz.gif", seed, sizeof(seed));
	fuzz_one(seed, n);
	mutate_and_run(seed, n, rounds);
	hist_clear();
	puts("ok");
	return 0;
}
#endif
