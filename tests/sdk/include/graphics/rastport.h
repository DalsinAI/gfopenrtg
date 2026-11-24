/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * For tests/sdk/test_sdk_present.sh on the PC: the structs a kit header
 * names by pointer only. */
#ifndef GRAPHICS_RASTPORT_H
#define GRAPHICS_RASTPORT_H
#include <exec/types.h>
struct BitMap;
struct RastPort;
struct Rectangle { WORD MinX, MinY, MaxX, MaxY; };
#endif
