/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library as a face of GL.module: what the generated table and thunks (gen_wrap.py), the call routine
 * (mglw_call.s) and the hand-written calls share. The dispatch table's types and sizes are MiniGL's own
 * (../mgl_entries.h): the wrapper keeps ABI 5, 840 bytes, 205 entries, the context as each call's first argument,
 * a 4-byte GLboolean and doubles as two longs. */
#ifndef MGLW_H
#define MGLW_H

#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/tasks.h>

#include "../mgl_internal.h"
#include "mglw_slots.h"

extern const MGLDispatchTable mglw_table;
extern const char *const mglw_gl_names[MGLW_SLOTS];

/* A session: one opening of GL.module (its own copy of Mesa's data) for the task that made its first context, and
 * the GL calls bound in it. mglw_call.s reads the first six fields at these offsets. */
struct mglw_context;
struct GLModuleTable;
struct gla_display;
struct mglw_session {
    APTR a4;                            /*  0: the module's A4 for this copy */
    APTR *fn;                           /*  4: MGLW_SLOTS functions, by slot */
    struct StackSwapStruct *sss;        /*  8: the GL stack, or NULL: GL runs on the task's own */
    struct Task *task;                  /* 12: the task the GL stack is for */
    ULONG depth;                        /* 16: calls on the GL stack now */
    struct mglw_context *current;       /* 20: the context made current in the module */
    /* the rest is mglw_ctx.c's */
    struct MinNode node;
    struct Task *owner;                 /* the task whose contexts these are */
    ULONG contexts;                     /* contexts in it; at 0 it is closed */
    APTR handle;                        /* OGPU_ModuleOpen's */
    struct GLModuleTable *module;
    struct gla_display *display;
    ULONG virgl;                        /* the display is virgl's (else softpipe: the 68k route) */
    APTR stack_mem;
};

/* What every wrapped context starts with: mglw_call.s reads the session from a GLcontext's first long. The rest
 * of the context is mglw_ctx.c's. */
struct mglw_context_head {
    struct mglw_session *s;
};

/* Call fn(args...) in session s: A4 set to the module's, on the GL stack when the calling task is the session's
 * and its own stack is small. n is the number of longs that follow. */
ULONG mglw_invoke(struct mglw_session *s, APTR fn, ULONG n, ...);

/* mglw_call.s calls this, with the context, before a GL call whose context isn't the session's current one. */
void mglw_switch(struct mglw_context *c);

/* A GL call GL.module doesn't have: does nothing, answers 0. */
ULONG mglw_missing(void);

/* A slot's function in a context's session, for the hand-written calls:
 *   mglw_invoke(S(c), F(c, glEnable), 1, GL_TEXTURE_2D); */
#define MGLW_S(c)       (((struct mglw_context_head *)(c))->s)
#define MGLW_F(c, name) (MGLW_S(c)->fn[MGLW_SLOT_##name])

#endif
