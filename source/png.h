// PNG export of a frame, scaled with nearest-neighbor (crisp pixels).
// SPDX-License-Identifier: MIT
#pragma once
#include "doc.h"

#define PNG_MAX_DIM 2560

// Returns NULL on success, or a short error message.
const char* png_export(const Doc* d, int frame, int outW, int outH, const char* path);
