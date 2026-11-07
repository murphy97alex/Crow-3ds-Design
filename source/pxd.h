// .pxd project files. Format: docs/PXD.md
// SPDX-License-Identifier: MIT
#pragma once
#include "doc.h"

#define PXD_VERSION 1

// Returns NULL on success, or a short error message.
const char* pxd_save(const Doc* d, const char* path);
const char* pxd_load(Doc* d, const char* path);
