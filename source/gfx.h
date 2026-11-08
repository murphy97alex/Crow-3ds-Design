// GPU textures and drawing helpers.
// SPDX-License-Identifier: MIT
#pragma once
#include <citro2d.h>
#include "doc.h"

bool gfx_init(void);
void gfx_fini(void);
void gfx_frame_begin(void);  // resets the per-frame text buffer

// Canvas texture mirrors the current frame of the document.
void gfx_canvas_rebuild(const Doc* d);
void gfx_canvas_flush(void);
// Draw the canvas (with checkerboard behind transparent pixels) at (x,y), scaled.
void gfx_draw_canvas(const Doc* d, float x, float y, float scale, float clipY0, float clipY1);

// Unclipped canvas draw for previews; flipX mirrors it left-right.
void gfx_draw_canvas_ex(const Doc* d, float x, float y, float scale, bool flipX);

// Checkerboard rectangle, cell = checker square size in screen pixels.
void gfx_draw_checker(float x, float y, float w, float h, float cell);

void gfx_text(float x, float y, float scale, u32 color, const char* fmt, ...)
	__attribute__((format(printf, 5, 6)));
float gfx_text_width(float scale, const char* s);
void gfx_rect_outline(float x, float y, float w, float h, float t, u32 color);
