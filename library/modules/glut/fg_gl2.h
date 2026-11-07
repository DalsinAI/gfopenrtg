/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GLUT on GFOpenGPU: stands in for freeglut's fg_gl2.h. freeglut looks GL
 * 2.0's buffer and attribute calls up at run time, because some systems'
 * GL headers lack them; libGL.a has every GL call, so the geometry calls
 * them directly, as freeglut does on GLES. */
#ifndef GLUT_FG_GL2_H
#define GLUT_FG_GL2_H

#include <GL/freeglut.h>
#include "fg_internal.h"

#define FGH_ARRAY_BUFFER GL_ARRAY_BUFFER
#define FGH_STATIC_DRAW GL_STATIC_DRAW
#define FGH_ELEMENT_ARRAY_BUFFER GL_ELEMENT_ARRAY_BUFFER

#define fghGenBuffers glGenBuffers
#define fghDeleteBuffers glDeleteBuffers
#define fghBindBuffer glBindBuffer
#define fghBufferData glBufferData
#define fghEnableVertexAttribArray glEnableVertexAttribArray
#define fghDisableVertexAttribArray glDisableVertexAttribArray
#define fghVertexAttribPointer glVertexAttribPointer

#endif
