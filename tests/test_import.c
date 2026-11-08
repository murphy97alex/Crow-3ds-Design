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

	uint32_t* rgba; int w, h;
	assert(import_read_png("imp.png", &rgba, &w, &h) == NULL);
	assert(w == 8 && h == 4 && rgba[0] == 0xFF0000FF && (rgba[7] & 255) == 0);

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
	assert(import_read_png("nope.png", &rgba, &w, &h) != NULL);
	doc_free(&d);
	doc_free(&e);
	hist_clear();
	puts("ok");
	return 0;
}
