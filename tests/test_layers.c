// Host tests: layers, folders, merge, composite, resize/crop, whole-document undo, .pxd layers.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doc.h"
#include "pxd.h"

static Doc d;
static const char* names(void)   // top -> bottom, folder members shown as "[name]"
{
	static char buf[256];
	buf[0] = 0;
	for (int i = d.layerCount - 1; i >= 0; i--)
	{
		char t[40];
		snprintf(t, sizeof(t), d.layers[i].folder >= 0 ? "[%s] " : "%s ", d.layers[i].name);
		strcat(buf, t);
	}
	return buf;
}
static void rename_all(void) { for (int i = 0; i < d.layerCount; i++) snprintf(d.layers[i].name, LAYER_NAME_MAX, "%c%d", d.layers[i].isFolder ? 'F' : 'L', d.layers[i].id); }
static void check(const char* want)
{
	if (strcmp(names(), want)) { printf("got  '%s'\nwant '%s'\n", names(), want); assert(0); }
}

int main(void)
{
	assert(doc_new(&d, 4, 2));
	assert(d.layerCount == 1 && d.curLayer == 0 && !d.layers[0].isFolder);

	// add layers and a folder
	int l2 = doc_layer_add(&d, false);           // above L1, becomes current
	assert(l2 == 1 && d.curLayer == 1);
	int f3 = doc_layer_add(&d, true);            // folder above L2
	assert(f3 == 2 && d.curLayer == 1);
	rename_all();
	check("F3 L2 L1 ");

	// move L2 up -> joins the empty folder above it; up again -> leaves it
	assert(doc_layer_move(&d, 1, +1) == 1); check("F3 [L2] L1 ");
	assert(doc_folder_size(&d, 2) == 1);
	assert(doc_layer_move(&d, 1, +1) == 2); check("L2 F3 L1 ");
	// move L2 down -> enters the folder at the top
	assert(doc_layer_move(&d, 2, -1) == 1); check("F3 [L2] L1 ");
	// bottom member moving down leaves the folder (stays in place)
	assert(doc_layer_move(&d, 1, -1) == 1); check("F3 L2 L1 ");
	assert(doc_layer_move(&d, 1, +1) == 1); check("F3 [L2] L1 ");

	// add a layer while inside the folder -> new member
	d.curLayer = 1;
	int l4 = doc_layer_add(&d, false);
	rename_all();
	assert(l4 == 2 && d.layers[2].folder == d.layers[3].id); check("F3 [L4] [L2] L1 ");

	// move the folder block down past L1 and back up
	assert(doc_layer_move(&d, 3, -1) == 2); check("L1 F3 [L4] [L2] ");
	assert(doc_layer_move(&d, 2, +1) == 3); check("F3 [L4] [L2] L1 ");
	assert(d.layers[d.curLayer].id == 4);  // current layer followed the move

	// effective visibility / opacity / lock through the folder
	d.layers[3].opacity = 128;
	d.layers[2].opacity = 128;
	assert(doc_layer_opacity(&d, 2) == 64 && doc_layer_opacity(&d, 0) == 255);
	d.layers[3].visible = false;
	assert(!doc_layer_visible(&d, 1) && doc_layer_visible(&d, 0));
	d.layers[3].visible = true;
	d.layers[3].locked = true;
	assert(doc_layer_locked(&d, 2) && !doc_layer_locked(&d, 0));
	d.layers[3].locked = false;
	d.layers[3].opacity = 255;

	// composite: L1 red, L2 (in folder) blue at 100%, L4 white at 50%
	uint8_t R = doc_color_index(&d, 0xFF0000FF, true), B = doc_color_index(&d, 0x0000FFFF, true), W = doc_color_index(&d, 0xFFFFFFFF, true);
	d.layers[0].cels[0][0] = R; d.layers[0].cels[0][1] = R;
	d.layers[1].cels[0][1] = B;
	d.layers[2].cels[0][0] = W; d.layers[2].cels[0][3] = W;
	uint32_t out[8];
	doc_composite(&d, 0, out);
	assert(out[1] == 0x0000FFFF);                       // blue over red
	assert((out[0] >> 24) >= 0xFE && ((out[0] >> 16) & 255) >= 0x7E && ((out[0] >> 16) & 255) <= 0x80 && (out[0] & 255) == 255);
	assert((out[3] & 255) >= 127 && (out[3] & 255) <= 128);  // 50% white on nothing
	assert(out[2] == 0);

	// undo covers layer structure
	hist_push(&d);
	assert(doc_layer_delete(&d, 3));                    // folder + both members
	rename_all();
	check("L1 ");
	assert(!doc_layer_delete(&d, 0));                   // keep at least one drawable layer
	assert(hist_undo(&d));
	rename_all();
	check("F3 [L4] [L2] L1 ");
	assert(d.layers[1].cels[0][1] == B);

	// merge down L4 (50% white) into L2 (blue): pixel 0 white on empty -> white (>=50%), pixel 3 too
	d.curLayer = 2;
	assert(doc_layer_merge_down(&d, 2));
	rename_all();
	check("F3 [L2] L1 ");
	assert(d.layers[1].cels[0][0] == W && d.layers[1].cels[0][1] == B);

	// merge visible -> into L1, folder stays
	assert(doc_merge_visible(&d));
	rename_all();
	check("F3 L1 ");
	assert(doc_color(&d, d.layers[0].cels[0][1]) == 0x0000FFFF && doc_color(&d, d.layers[0].cels[0][0]) == 0xFFFFFFFF);

	// duplicate + resize/crop keep all layers
	int dup = doc_layer_duplicate(&d, 0);
	assert(dup == 1 && d.curLayer == 1);
	assert(doc_resize(&d, 6, 4, 1, 1));                  // centered: +1, +1
	assert(d.w == 6 && d.h == 4 && doc_color(&d, d.layers[1].cels[0][1 * 6 + 2]) == 0x0000FFFF);
	assert(doc_crop(&d, 2, 1, 2, 1));
	assert(d.w == 2 && d.h == 1 && doc_color(&d, d.layers[0].cels[0][0]) == 0x0000FFFF);

	// .pxd round trip with layers and folders
	doc_new(&d, 3, 3);
	doc_layer_add(&d, false);
	doc_layer_add(&d, true);
	doc_layer_move(&d, 1, +1);                          // layer 2 into the folder
	d.layers[1].opacity = 77;
	d.layers[0].locked = true;
	d.layers[0].cels[0][4] = doc_color_index(&d, 0x123456FF, true);
	snprintf(d.layers[2].name, LAYER_NAME_MAX, "Eyes");
	assert(pxd_save(&d, "layers.pxd") == NULL);
	Doc e = {0};
	assert(pxd_load(&e, "layers.pxd") == NULL);
	assert(e.layerCount == 3 && e.layers[2].isFolder && !strcmp(e.layers[2].name, "Eyes"));
	assert(e.layers[1].folder == e.layers[2].id && e.layers[1].opacity == 77 && e.layers[0].locked);
	assert(doc_color(&e, e.layers[0].cels[0][4]) == 0x123456FF && e.curLayer == d.curLayer);
	assert(e.nextLayerId > e.layers[2].id);
	doc_free(&e);

	// thumbnails: stored on save, read back without loading the project
	{
		int w, h, layers, frames, tw, th, tw2, th2;
		bool has;
		static uint32_t t1[PXD_THUMB * PXD_THUMB], t2[PXD_THUMB * PXD_THUMB];
		assert(pxd_read_info("layers.pxd", &w, &h, &layers, &frames, &has, t1, &tw, &th));
		assert(w == 3 && h == 3 && layers == 3 && frames == d.frameCount && has && tw == 63 && th == 63);   // 3px * 21
		pxd_make_thumb(&d, t2, &tw2, &th2);
		assert(tw2 == tw && th2 == th && !memcmp(t1, t2, tw * th * 4));
		assert(t1[(1 * 21 + 5) * 63 + 1 * 21 + 5] == 0x123456FF);                  // centre pixel
		// a wide canvas shrinks to fit
		Doc big = {0};
		doc_new(&big, 320, 100);
		pxd_make_thumb(&big, t2, &tw2, &th2);
		assert(tw2 == 64 && th2 == 20);
		doc_free(&big);
		assert(!pxd_read_info("missing.pxd", &w, &h, &layers, &frames, &has, t1, &tw, &th));
	}

	doc_free(&d);
	hist_clear();
	puts("ok");
	return 0;
}
