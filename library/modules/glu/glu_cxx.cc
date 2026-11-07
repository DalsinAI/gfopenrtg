/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * What GLU's NURBS (C++, built without exceptions or RTTI) needs from a C++
 * runtime, so a C program links -lGLU with gcc and no libstdc++: operator
 * new and delete on malloc, and the pure virtual trap. They are weak, so a
 * C++ program's own (libstdc++'s) win. */
#include <stdlib.h>
#include <stddef.h>

#define WEAK __attribute__((weak))

WEAK void *operator new(size_t n) { return malloc(n ? n : 1); }
WEAK void *operator new[](size_t n) { return malloc(n ? n : 1); }
WEAK void operator delete(void *p) noexcept { free(p); }
WEAK void operator delete[](void *p) noexcept { free(p); }
WEAK void operator delete(void *p, size_t) noexcept { free(p); }
WEAK void operator delete[](void *p, size_t) noexcept { free(p); }

extern "C" WEAK void __cxa_pure_virtual(void) { abort(); }
