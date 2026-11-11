// Button remapping: left-handed mode and Old 3DS (no ZL / ZR).
// Pure C so it can be unit-tested on a PC. The IN_* bits match libctru's KEY_* values
// (main.c checks that at compile time).
// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stdint.h>

enum {
	IN_A = 1u << 0, IN_B = 1u << 1, IN_SELECT = 1u << 2, IN_START = 1u << 3,
	IN_DRIGHT = 1u << 4, IN_DLEFT = 1u << 5, IN_DUP = 1u << 6, IN_DDOWN = 1u << 7,
	IN_R = 1u << 8, IN_L = 1u << 9, IN_X = 1u << 10, IN_Y = 1u << 11,
	IN_ZL = 1u << 14, IN_ZR = 1u << 15, IN_TOUCH = 1u << 20,
};

typedef struct {
	bool lefty;      // stylus in the left hand: the button layout is mirrored
	bool old3ds;     // no ZL / ZR: SELECT (held) stands in for ZR, R + L for ZL
	bool selUsed;    // internal: SELECT was used as a modifier during this press
} InputMap;

// Mirror a button mask left-right: D-pad <-> A/B/X/Y (up-X, down-B, left-A, right-Y),
// L <-> R, ZL <-> ZR. Other bits pass through.
uint32_t input_mirror(uint32_t keys);

// Remap one frame of input (pressed / held / released masks) in place. Call once per frame
// while drawing.
//  - lefty: all three masks are mirrored.
//  - old3ds: while SELECT is held it reads as ZR held. A SELECT press only arrives (as a press)
//    when the button is let go without having touched the screen meanwhile, so a plain tap
//    still works. Pressing L while holding R reads as ZL (redo) instead of L.
void input_map(InputMap* m, uint32_t* down, uint32_t* held, uint32_t* up);
