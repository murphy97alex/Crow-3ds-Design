// Host tests: frames, durations, tags, linked cels, cel-level undo, .pxd v4.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doc.h"
#include "pxd.h"

static Doc d;
static uint8_t* cel(int l, int f) { return d.layers[l].cels[f]; }

int main(void)
{
	assert(doc_new(&d, 4, 4));
	uint8_t red = doc_color_index(&d, 0xFF0000FF, true), blue = doc_color_index(&d, 0x0000FFFF, true);
	cel(0, 0)[0] = red;

	// insert copy / blank, curFrame follows
	assert(doc_frame_insert(&d, 1, 0) && d.frameCount == 2 && d.curFrame == 1 && cel(0, 1)[0] == red && cel(0, 1) != cel(0, 0));
	assert(doc_frame_insert(&d, 2, -1) && d.frameCount == 3 && cel(0, 2)[0] == 0);
	d.frameMs[2] = 250;
	assert(doc_frame_ms(&d, 2) == 250 && doc_frame_ms(&d, 0) == 125);   // 8 fps default

	// tags follow frame edits
	d.tagCount = 1;
	snprintf(d.tags[0].name, TAG_NAME_MAX, "walk");
	d.tags[0].from = 1; d.tags[0].to = 2; d.tags[0].dir = TAG_PINGPONG;
	assert(doc_frame_insert(&d, 1, -1) && d.tags[0].from == 2 && d.tags[0].to == 3);
	assert(doc_frame_delete(&d, 1) && d.tags[0].from == 1 && d.tags[0].to == 2 && d.frameCount == 3);
	assert(d.frameMs[2] == 250);

	// move
	assert(doc_frame_move(&d, 2, -1) && d.frameMs[1] == 250 && cel(0, 1)[0] == 0 && cel(0, 2)[0] == red);
	assert(!doc_frame_move(&d, 0, -1));

	// link frame 1 to frame 0: same drawing, edits show in both
	assert(doc_cel_link_prev(&d, 0, 1) && cel(0, 1) == cel(0, 0) && doc_cel_linked(&d, 0, 0));
	cel(0, 1)[5] = blue;
	assert(cel(0, 0)[5] == blue);
	// compaction visits a linked cel only once (would scramble values otherwise)
	uint8_t g = doc_color_index(&d, 0x00FF00FF, true);   // unused color, removed by compact
	(void)g;
	doc_compact(&d);
	assert(doc_color(&d, cel(0, 0)[0]) == 0xFF0000FF && doc_color(&d, cel(0, 1)[5]) == 0x0000FFFF);
	// resize keeps the link
	assert(doc_resize(&d, 6, 6, 0, 0) && cel(0, 0) == cel(0, 1) && doc_color(&d, cel(0, 1)[0]) == 0xFF0000FF);
	// whole-document undo keeps links
	hist_push(&d);
	assert(doc_frame_delete(&d, 0) && d.frameCount == 2 && !doc_cel_linked(&d, 0, 0));
	assert(hist_undo(&d) && d.frameCount == 3 && cel(0, 0) == cel(0, 1));
	// unlink gives frame 1 its own copy
	assert(doc_cel_unlink(&d, 0, 1) && cel(0, 1) != cel(0, 0) && cel(0, 1)[0] == cel(0, 0)[0]);
	cel(0, 1)[0] = 0;
	assert(cel(0, 0)[0] != 0);
	// deleting a frame with a linked cel doesn't free the shared buffer
	assert(doc_cel_link_prev(&d, 0, 1) && doc_frame_delete(&d, 1) && doc_color(&d, cel(0, 0)[0]) == 0xFF0000FF);

	// cel-level undo survives color-table compaction
	hist_clear();
	d.curFrame = 0;
	uint8_t* c0 = cel(0, 0);
	uint8_t grn = doc_color_index(&d, 0x00AA00FF, true);
	hist_begin_cel(&d);
	c0[2] = grn;
	hist_commit();
	c0[3] = doc_color_index(&d, 0x123456FF, true);    // later edit without history
	doc_compact(&d);                                  // renumbers colors
	assert(hist_undo(&d));
	assert(c0[2] == 0 && doc_color(&d, c0[0]) == 0xFF0000FF);
	assert(hist_redo(&d) && doc_color(&d, c0[2]) == 0x00AA00FF);

	// .pxd round trip: durations, link, tags
	assert(doc_frame_insert(&d, 1, -1) && doc_cel_link_prev(&d, 0, 1));
	d.frameMs[1] = 333;
	assert(pxd_save(&d, "frames.pxd") == NULL);
	Doc e = {0};
	assert(pxd_load(&e, "frames.pxd") == NULL);
	assert(e.frameCount == d.frameCount && e.frameMs[1] == 333 && e.layers[0].cels[0] == e.layers[0].cels[1]);
	assert(e.tagCount == 1 && !strcmp(e.tags[0].name, "walk") && e.tags[0].dir == TAG_PINGPONG);
	assert(doc_color(&e, e.layers[0].cels[1][0]) == 0xFF0000FF);
	doc_free(&e);

	doc_free(&d);
	hist_clear();
	puts("ok");
	return 0;
}
