/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * For tests/test_gla_present.c on the PC: just what gla_present_os3.c uses. */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stddef.h>                 /* NULL, as the NDK's exec/types.h gives it */
typedef unsigned long ULONG;        /* pointer-sized on the PC: LockBitMapTags' tags carry addresses */
typedef long LONG;
typedef unsigned short UWORD;
typedef short WORD;
typedef unsigned char UBYTE;
typedef short BOOL;
typedef void *APTR;
typedef char *STRPTR;
typedef const char *CONST_STRPTR;
#define TAG_DONE 0UL
#define TAG_END 0UL
struct Library { UWORD lib_Version, lib_Revision; };
struct BitMap { int id; };
struct RastPort { struct BitMap *BitMap; };
#endif
