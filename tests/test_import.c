// Host test: PNG import (round trip through our exporter).
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "doc.h"
#include "import.h"
#include "export_png.h"

int main(void)
{
	Doc d = {0};
	doc_new(&d, 4, 2);
	uint8_t r = doc_color_index(&d, 0xFF0000FF, true), g = doc_color_index(&d, 0x00FF00FF, true);
	uint8_t* px = d.layers[0].cels[0];
	px[0] = r; px[1] = r; px[2] = r; px[5] = g;
	assert(png_export(&d, 0, 8, 4, "imp.png") == NULL);

	uint32_t* rgba; int w, h; bool shrunk;
	assert(import_read_png("imp.png", 320, 240, &rgba, &w, &h, &shrunk) == NULL);
	assert(w == 8 && h == 4 && !shrunk && rgba[0] == 0xFF0000FF && (rgba[7] & 255) == 0);
	// shrinking while reading
	uint32_t* small; int sw2, sh2;
	assert(import_read_png("imp.png", 4, 4, &small, &sw2, &sh2, &shrunk) == NULL);
	assert(sw2 == 4 && sh2 == 2 && shrunk && small[0] == 0xFF0000FF && small[5] == 0x00FF00FF);
	free(small);
	// the big palette PNG from testing, if present
	if (import_read_png("big.png", 320, 240, &small, &sw2, &sh2, &shrunk) == NULL)
	{
		printf("big.png -> %dx%d\n", sw2, sh2);
		assert(shrunk && sh2 == 240 && sw2 <= 320);
		free(small);
	}

	uint32_t pal[4];
	assert(import_palette(rgba, w, h, pal, 4) == 2 && pal[0] == 0xFF0000FF && pal[1] == 0x00FF00FF);

	assert(import_fit(&rgba, &w, &h, 4, 4) && w == 4 && h == 2);
	assert(rgba[0] == 0xFF0000FF && rgba[5] == 0x00FF00FF);

	Doc e = {0};
	doc_new(&e, 4, 2);
	uint8_t* ix = import_to_indexed(&e, rgba, w, h);
	assert(ix && doc_color(&e, ix[0]) == 0xFF0000FF && ix[3] == 0 && doc_color(&e, ix[5]) == 0x00FF00FF && e.colorCount == 2);
	free(ix);
	free(rgba);
	assert(import_read_png("nope.png", 320, 240, &rgba, &w, &h, &shrunk) != NULL);
	doc_free(&d);
	doc_free(&e);
	hist_clear();
	puts("ok");
	return 0;
}
