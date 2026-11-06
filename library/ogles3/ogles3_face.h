/* Copyright (c) 2026 Dalsin Limited / the AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ogles3.library on GFOpenGPU: what ogles3_lib.c, ogles3_call.s and the
 * generated ogles3_table.c share. */
#ifndef OGLES3_FACE_H
#define OGLES3_FACE_H

#include <exec/types.h>
#include <exec/nodes.h>

/* The dispatch table's ABI (ogles3's <libraries/ogles3_dispatch.h>):
 * four longs, then 380 calls, each a plain C function with its arguments
 * on the stack; the GL ones take the context first. */
#define OGL3_DISPATCH_ABI_VERSION 1
#define OGL3_SLOTS 380
#define OGL3_AGL_SLOTS 6
/* backendFlags: ogles3's own build says 1 (PiStorm's V3D 4.2); this one
 * draws through GFOpenGPU's GL module. */
#define OGL3_BACKEND_GFOPENGPU 0x100

struct ogl3_dispatch {
    ULONG abiVersion;
    ULONG structSize;
    ULONG backendFlags;
    ULONG reserved;
    APTR entry[OGL3_SLOTS];
};
extern const struct ogl3_dispatch ogl3_dispatch;
extern const char *const ogl3_gl_names[OGL3_SLOTS];

/* A session: one opening of GL.module (its own copy of Mesa's data) for the
 * task that made its first context, and the GL calls bound in it. The
 * first six fields are read by ogles3_call.s at these offsets. */
struct ogl3_context;
struct ogl3_session {
    APTR a4;                            /*  0: the module's A4 for this copy */
    APTR *fn;                           /*  4: OGL3_SLOTS functions, by slot */
    struct StackSwapStruct *sss;        /*  8: the GL stack, or NULL: GL runs on the task's own */
    struct Task *task;                  /* 12: the task the GL stack is for */
    ULONG depth;                        /* 16: calls on the GL stack now */
    struct ogl3_context *current;       /* 20: the context made current in the module */
    /* the rest is ogles3_lib.c's */
    struct MinNode node;
    struct Task *owner;                 /* the task whose contexts these are */
    ULONG contexts;                     /* contexts in it; at 0 it is closed */
    APTR handle;                        /* OGPU_ModuleOpen's */
    struct GLModuleTable *module;
    struct gla_display *display;
    ULONG virgl;                        /* the display is virgl's (else softpipe) */
    APTR stack_mem;
};

/* Call fn(args...) in session s: A4 set to the module's, on the GL stack
 * when the calling task is the session's and its own stack is small. n is
 * the number of longs that follow. */
ULONG ogl3_invoke(struct ogl3_session *s, APTR fn, ULONG n, ...);

/* ogles3_call.s calls this, with the context, before a GL call whose
 * context isn't the session's current one. */
void ogl3_switch(struct ogl3_context *c);

/* A GL call GL.module doesn't have: does nothing, answers 0. */
ULONG ogl3_missing(void);

/* The agl calls (ogles3_lib.c), entries 0 to 5. */
struct TagItem;
struct BitMap;
struct ogl3_context *ogl3_aglCreateContext2(ULONG *errcode, struct TagItem *tags);
void ogl3_aglDestroyContext(struct ogl3_context *c);
void ogl3_aglMakeCurrent(struct ogl3_context *c);
void ogl3_aglSwapBuffers(struct ogl3_context *c);
void ogl3_aglSetParams2(struct ogl3_context *c, struct TagItem *tags);
void ogl3_aglSetBitmap(struct ogl3_context *c, struct BitMap *bm);

#endif
