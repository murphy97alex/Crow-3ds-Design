// SPDX-License-Identifier: MIT
#include "input.h"

static uint32_t swap_bits(uint32_t keys, uint32_t a, uint32_t b)
{
	uint32_t out = keys & ~(a | b);
	if (keys & a) out |= b;
	if (keys & b) out |= a;
	return out;
}

uint32_t input_mirror(uint32_t keys)
{
	keys = swap_bits(keys, IN_DUP, IN_X);
	keys = swap_bits(keys, IN_DDOWN, IN_B);
	keys = swap_bits(keys, IN_DLEFT, IN_A);
	keys = swap_bits(keys, IN_DRIGHT, IN_Y);
	keys = swap_bits(keys, IN_L, IN_R);
	keys = swap_bits(keys, IN_ZL, IN_ZR);
	return keys;
}

void input_map(InputMap* m, uint32_t* down, uint32_t* held, uint32_t* up)
{
	if (m->lefty)
	{
		*down = input_mirror(*down);
		*held = input_mirror(*held);
		*up = input_mirror(*up);
	}
	if (!m->old3ds)
		return;

	// SELECT held = ZR. Its own press is delivered on release, unless it was used with the stylus.
	if (*down & IN_SELECT) m->selUsed = false;
	if ((*held & IN_SELECT) && (*held & IN_TOUCH)) m->selUsed = true;
	*down &= ~IN_SELECT;
	if (*held & IN_SELECT)
	{
		if (!(*held & IN_ZR)) *held |= IN_ZR;
		*held &= ~IN_SELECT;
	}
	if (*up & IN_SELECT)
	{
		*up &= ~IN_SELECT;
		*up |= IN_ZR;
		if (!m->selUsed) *down |= IN_SELECT;
		m->selUsed = false;
	}
	// R + L = redo
	if ((*held & IN_R) && (*down & IN_L))
	{
		*down &= ~IN_L;
		*down |= IN_ZL;
	}
}
