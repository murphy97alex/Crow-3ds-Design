// Color picker for a palette slot: hue/saturation wheel, brightness slider, hex entry.
// SPDX-License-Identifier: MIT
#pragma once
#include <3ds.h>
#include <stdint.h>

typedef enum { PICK_OPEN, PICK_OK, PICK_CANCEL, PICK_LIST } PickResult;

void picker_open(int slot, uint32_t rgba, const uint32_t* palette);
PickResult picker_update(u32 kDown, u32 kHeld, touchPosition touch);
void picker_draw(void);          // bottom screen
uint32_t picker_color(void);
void picker_set_linked(bool linked);  // shows a note that the drawing will change too

// After PICK_LIST: colors typed for slots 1..n (n <= 32).
int picker_list(uint32_t* out);
