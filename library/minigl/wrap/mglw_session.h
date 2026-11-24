/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library as a face of GL.module: sessions (mglw_session.c). A task that makes contexts gets one: its own
 * opening of GL.module, so its own copy of Mesa's data, a GLA display (virgl when gfopengpu.library carries it,
 * else softpipe: MiniGL's 68k route), and the module's calls bound by name. */
#ifndef MGLW_SESSION_H
#define MGLW_SESSION_H

#include "mglw.h"

/* The GLA calls the wrapper makes, bound by name in each session. */
enum {
    MGLW_G_BUFFER_CREATE, MGLW_G_BUFFER_DESTROY, MGLW_G_BUFFER_RESIZE, MGLW_G_CONTEXT_CREATE,
    MGLW_G_CONTEXT_DESTROY, MGLW_G_DISPLAY_CREATE, MGLW_G_DISPLAY_CREATE_VIRGL, MGLW_G_DISPLAY_DESTROY,
    MGLW_G_MAKE_CURRENT, MGLW_G_OS3_PRESENT_INIT, MGLW_G_OS3_VIRGL_TRANSPORT, MGLW_G_SWAP, MGLW_G_COUNT
};

/* A GLA call in a session: n arguments of a long each (up to five). 0 when the module lacks it. */
ULONG mglw_gla(struct mglw_session *s, int which, ULONG n, ULONG a, ULONG b, ULONG c, ULONG d, ULONG e);

/* The calling task's session with one more context counted in it, made when the task has none; NULL when
 * GL.module can't be opened (none in LIBS:GFOpenGPU, one too old, no memory, or the caller isn't a process). */
struct mglw_session *mglw_session_get(void);

/* One context fewer; the session ends with its last. */
void mglw_session_release(struct mglw_session *s);

/* Once, from the library's init. */
void mglw_sessions_init(void);

#endif
