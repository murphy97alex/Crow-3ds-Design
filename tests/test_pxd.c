// Host-side test for .pxd save/load and undo history: cc -I source tests/test_pxd.c source/pxd.c source/doc.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doc.h"
#include "pxd.h"

int main(void)
{
	Doc a = {0}, b = {0};
	assert(doc_new(&a, 48, 30));
	for (int i = 0; i < 48 * 30; i++) a.frames[0][i] = (i * 7 / 13) % 33;
	a.palette[3] = 0x12345678;
	a.frames[1] = calloc(48 * 30, 1); a.frames[1][5] = 9; a.frameCount = 2;
	assert(pxd_save(&a, "test.pxd") == NULL);
	assert(pxd_load(&b, "test.pxd") == NULL);
	assert(b.w == 48 && b.h == 30 && b.frameCount == 2 && b.palette[3] == 0x12345678);
	assert(!memcmp(a.frames[0], b.frames[0], 48 * 30) && !memcmp(a.frames[1], b.frames[1], 48 * 30));

	// undo/redo
	Doc c = {0}; doc_new(&c, 4, 4);
	hist_push(&c); c.frames[0][0] = 1;
	hist_push(&c); c.frames[0][1] = 2;
	assert(hist_undo(&c) && c.frames[0][1] == 0 && c.frames[0][0] == 1);
	assert(hist_undo(&c) && c.frames[0][0] == 0);
	assert(!hist_undo(&c));
	assert(hist_redo(&c) && c.frames[0][0] == 1 && c.frames[0][1] == 0);
	assert(hist_redo(&c) && c.frames[0][1] == 2);
	assert(!hist_redo(&c));
	// empty stroke keeps redo
	assert(hist_undo(&c));
	hist_begin(&c); hist_abort();
	assert(hist_redo(&c) && c.frames[0][1] == 2);
	hist_begin(&c); c.frames[0][2] = 3; hist_commit();
	assert(hist_undo(&c) && c.frames[0][2] == 0);

	FILE* f = fopen("bad.pxd", "wb"); fwrite("PXD\x1A" "HEAD\xff\xff\xff\x7f", 1, 12, f); fclose(f);
	assert(pxd_load(&b, "bad.pxd") != NULL && b.w == 48);
	doc_free(&a); doc_free(&b); doc_free(&c); hist_clear();
	puts("ok");
	return 0;
}
