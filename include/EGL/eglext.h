/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * EGL on GFOpenGPU: the extensions libEGL.a has (eglQueryString's
 * EGL_EXTENSIONS lists the same). Khronos's names and values. */
#ifndef __eglext_h_
#define __eglext_h_ 1

#include <EGL/egl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Contexts by version and profile, and ES 3 configs. */
#define EGL_KHR_create_context 1
#define EGL_CONTEXT_MAJOR_VERSION_KHR   0x3098
#define EGL_CONTEXT_MINOR_VERSION_KHR   0x30FB
#define EGL_CONTEXT_FLAGS_KHR           0x30FC
#define EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR 0x30FD
#define EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR 0x31BD
#define EGL_NO_RESET_NOTIFICATION_KHR   0x31BE
#define EGL_LOSE_CONTEXT_ON_RESET_KHR   0x31BF
#define EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR 0x00000001
#define EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR 0x00000002
#define EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR 0x00000004
#define EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR 0x00000001
#define EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR 0x00000002
#define EGL_OPENGL_ES3_BIT_KHR          0x00000040

/* eglGetProcAddress gives EGL's own calls and every GL and GLES call. */
#define EGL_KHR_get_all_proc_addresses 1
#define EGL_KHR_client_get_all_proc_addresses 1

/* eglMakeCurrent with a context and no surface. */
#define EGL_KHR_surfaceless_context 1

#ifdef __cplusplus
}
#endif
#endif
