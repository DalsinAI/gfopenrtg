/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * For tests/sdk/test_sdk_present.sh on the PC (built -m32): the types a kit
 * header takes from the NDK's exec/types.h, at the Amiga's sizes. */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stddef.h>
#define VOID void
#define CONST const
typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned short UWORD;
typedef short WORD;
typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef short BOOL;
typedef void *APTR;
typedef char *STRPTR;
typedef const char *CONST_STRPTR;
#endif
