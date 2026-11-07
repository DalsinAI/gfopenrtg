/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The part of glad's loader that mesa-demos' programs use, for building
 * them on GFOpenGPU: libGL.a has every GL call, so there is nothing to load,
 * and gladLoaderLoadGL only notes the context's version. */
#ifndef GLAD_GL_H_
#define GLAD_GL_H_

#include <stdlib.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

static int glad_gl_version;     /* major * 10 + minor, after gladLoaderLoadGL */

static inline int gladLoaderLoadGL(void)
{
    const char *v = (const char *)glGetString(GL_VERSION);
    if (!v)
        return 0;
    glad_gl_version = atoi(v) * 10 + (v[1] == '.' ? atoi(v + 2) % 10 : 0);
    return glad_gl_version;
}

static inline void gladLoaderUnloadGL(void) {}

#define GLAD_GL_VERSION_1_0 (glad_gl_version >= 10)
#define GLAD_GL_VERSION_2_0 (glad_gl_version >= 20)
#define GLAD_GL_VERSION_2_1 (glad_gl_version >= 21)
#define GLAD_GL_VERSION_3_0 (glad_gl_version >= 30)
#define GLAD_GL_VERSION_3_3 (glad_gl_version >= 33)

#endif
