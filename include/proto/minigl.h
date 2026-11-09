/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library-compatible interface: the calls inline, through the table
 * (clean-room; see library/minigl/PROVENANCE.md). Include this before
 * <mgl/gl.h>, and link libminigl.a. */
#ifndef PROTO_MINIGL_H
#define PROTO_MINIGL_H

#if defined(MGL_MINIGL_CALLS_H) && !defined(MGL_CALLS_INLINE)
#error "include <proto/minigl.h> before <mgl/gl.h>, or link libmgl.a and leave it out"
#endif

#ifndef MGL_CALLS_INLINE
#define MGL_CALLS_INLINE
#endif
#include <libraries/minigl.h>
#include <mgl/gl.h>

#endif
