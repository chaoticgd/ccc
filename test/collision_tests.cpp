// This file is part of the Chaos Compiler Collection.
// SPDX-License-Identifier: MIT

// Make sure we don't have any name collisions.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "ccc/ccc.h"
