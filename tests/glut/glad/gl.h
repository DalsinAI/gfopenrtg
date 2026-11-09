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

/* major * 10 + minor, asked of the current context each time: a program's
 * files share no state (shaderutil.c tests it, the main file "loads" it). */
static inline int glad_gl_version_(void)
{
    const char *v = (const char *)glGetString(GL_VERSION);
    if (!v)
        return 0;
    return atoi(v) * 10 + (v[1] == '.' ? atoi(v + 2) % 10 : 0);
}

static inline int gladLoaderLoadGL(void) { return glad_gl_version_(); }

static inline void gladLoaderUnloadGL(void) {}

#define GLAD_GL_VERSION_1_0 (glad_gl_version_() >= 10)
#define GLAD_GL_VERSION_2_0 (glad_gl_version_() >= 20)
#define GLAD_GL_VERSION_2_1 (glad_gl_version_() >= 21)
#define GLAD_GL_VERSION_3_0 (glad_gl_version_() >= 30)
#define GLAD_GL_VERSION_3_3 (glad_gl_version_() >= 33)

#endif
