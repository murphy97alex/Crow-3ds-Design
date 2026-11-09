// Host test: text rendering.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"

int main(void)
{
	const PixFont* f = &FONTS[4];   // Crow Term: 5 wide + 1 advance, 9 tall
	assert(!strcmp(f->name, "Crow Term") && f->height == 9 && f->advance['A' - 32] == 6);
	int w, h;
	uint8_t* p = text_render(f, "AB", 1, 0, 7, 0, &w, &h);
	assert(p && w == 11 && h == 9);
	// 'A' top row is .###.
	assert(p[0] == 0 && p[1] == 7 && p[3] == 7 && p[4] == 0);
	free(p);
	// scale 2 + two lines
	p = text_render(f, "A\nA", 2, 0, 7, 0, &w, &h);
	assert(w == 10 && h == 38);
	assert(p[0] == 0 && p[2] == 7 && p[3] == 7 && p[w + 2] == 7);
	free(p);
	// outline adds a 1px border in the outline value
	p = text_render(f, "I", 1, 0, 7, 3, &w, &h);
	assert(w == 4 + 2 && h == 9 + 2);   // 'I' is .###. -> 4 columns after trimming
	int fg = 0, ol = 0;
	for (int i = 0; i < w * h; i++) { fg += p[i] == 7; ol += p[i] == 3; }
	assert(fg > 0 && ol > fg);
	assert(p[0] == 0);  // corner far from the I stays clear
	free(p);
	// empty text and unknown characters
	assert(text_render(f, "", 1, 0, 7, 0, &w, &h) == NULL);
	p = text_render(f, "\x01", 1, 0, 7, 0, &w, &h);   // renders as '?'
	assert(p && w == 5);
	free(p);
	// every font renders the full character set
	char all[96];
	for (int i = 0; i < 95; i++) all[i] = 32 + i;
	all[95] = 0;
	for (int i = 0; i < FONT_COUNT; i++)
	{
		p = text_render(&FONTS[i], all, 1, 0, 1, 0, &w, &h);
		assert(p && h == FONTS[i].height && w > 95 * 2);
		free(p);
	}
	puts("ok");
	return 0;
}
