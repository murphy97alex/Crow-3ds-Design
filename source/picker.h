// Color picker for a palette slot: hue/saturation wheel, brightness slider, hex entry.
// SPDX-License-Identifier: MIT
#pragma once
#include <3ds.h>
#include <stdint.h>

typedef enum { PICK_OPEN, PICK_OK, PICK_CANCEL } PickResult;

void picker_open(int slot, uint32_t rgba);
PickResult picker_update(u32 kDown, u32 kHeld, touchPosition touch);
void picker_draw(void);          // bottom screen
uint32_t picker_color(void);
