/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library-compatible interface: OpenGL 1.1 for programs of
 * minigl.library (clean-room; see library/minigl/PROVENANCE.md).
 *
 * Two ways to call:
 *   - through the table: include <proto/minigl.h> first (the calls are
 *     inline), link libminigl.a, call MiniGLOpen() and MiniGLClose();
 *   - as functions: include <mgl/gl.h>, link libmgl.a, call MGLInit() and
 *     MGLTerm().
 * Either way a call reads the current context through the table, and
 * mglCreateContext...() makes the new context the current one. */
#ifndef MGL_GL_H
#define MGL_GL_H

#include <mgl/mgltypes.h>
#include <mgl/gltokens.h>
#include <mgl/mgltokens.h>

/* The library's own sources (MINIGL_LIBRARY_BUILD) see no calls, only types. */
#if !defined(MINIGL_LIBRARY_BUILD) || defined(MGL_CALLS_INLINE) || defined(MGL_CALLS_DEFINE)
#include <mgl/minigl_calls.h>
#endif

#endif
