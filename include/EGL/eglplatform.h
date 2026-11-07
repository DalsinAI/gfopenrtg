/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * EGL on GFOpenGPU: the platform's types (libEGL.a, on GLA). There is one
 * display, the machine's; a window is an Intuition window (GL draws inside
 * its borders), and EGL has no pixmaps here. The names and values are
 * Khronos's, so EGL programs build unchanged. */
#ifndef __eglplatform_h_
#define __eglplatform_h_

#include <KHR/khrplatform.h>

#ifndef EGLAPI
#define EGLAPI extern
#endif
#ifndef EGLAPIENTRY
#define EGLAPIENTRY
#endif
#define EGLAPIENTRYP EGLAPIENTRY *

struct Window;
struct BitMap;

typedef void *EGLNativeDisplayType;     /* anything: EGL_DEFAULT_DISPLAY is the one there is */
typedef struct BitMap *EGLNativePixmapType;     /* none are taken (EGL_BAD_NATIVE_PIXMAP) */
typedef struct Window *EGLNativeWindowType;

/* EGL 1.0's names for them. */
typedef EGLNativeDisplayType NativeDisplayType;
typedef EGLNativePixmapType  NativePixmapType;
typedef EGLNativeWindowType  NativeWindowType;

typedef khronos_int32_t EGLint;

#endif
