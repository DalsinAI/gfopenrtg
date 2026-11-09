/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MADE BY library/minigl/gen_api.py FROM docs/design/MiniGL-Compatible-Spec.md
 * (section 8). Do not edit: change the generator or the spec.
 *
 * libmgl.a: every call a program makes, as a function, through the same
 * table: for programs written to call functions (MGLInit, MGLTerm). */
#define MGL_CALLS_DEFINE
#include <libraries/minigl.h>
#include <mgl/gl.h>

GLboolean MGLInit(void) { return MiniGLOpen() ? GL_TRUE : GL_FALSE; }
void MGLTerm(void) { MiniGLClose(); }
