/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * Host stand-ins for exec's headers: just what module_malloc.c uses,
 * for tests/modules/test_module_malloc.py. */
#ifndef HOST_EXEC_TYPES_H
#define HOST_EXEC_TYPES_H
#include <stdint.h>
typedef void *APTR;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uint16_t UWORD;
typedef int16_t WORD;
typedef uint8_t UBYTE;
typedef int8_t BYTE;
typedef char *STRPTR;
#endif
