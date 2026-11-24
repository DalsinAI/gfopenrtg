/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library as a face of GL.module: sessions. The same arrangement as ogles3.library's
 * (library/ogles3/ogles3_lib.c): each task that makes contexts opens GL.module for itself (OGPU_ModuleOpen), so
 * has its own copy of Mesa's data; the GL calls the table forwards are bound by name once (mglw_gl_names, generated);
 * Mesa runs on a 1 MB GL stack of the session's when the task's own is under 256 KB.
 *
 * The display is virgl's when gfopengpu.library carries it. Otherwise it is softpipe: that is MiniGL's 68k route
 * now (it needs an FPU and loads the whole GL module). ENV:MiniGL/Driver CPU takes softpipe on any machine.
 *
 * Compiled -ffixed-a4 with the rest of the wrapper: GL.module's calls set A4 (include/gfopengpu/module.h). */
#include <stddef.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "../../../include/gfopengpu/module.h"
#include "../../../stubs/gl/gl_module.h"
#include "../../modules/gl/gla/gla_core.h"
#include "../../modules/gl/gla/os3/gla_present_os3.h"

#define OPENGPU_BASE_NAME OpenGPUBase
#define GFOPENGPU_BASE_NAME OpenGPUBase
#include <inline/gfopengpu.h>

#include "mglw_session.h"

#define GL_STACK_MIN  (256 * 1024)
#define GL_STACK_SIZE (1024 * 1024)

extern struct Library *OpenGPUBase;

static const char *const gla_names[MGLW_G_COUNT] = {
    "gla_buffer_create", "gla_buffer_destroy", "gla_buffer_resize", "gla_context_create",
    "gla_context_destroy", "gla_display_create", "gla_display_create_virgl", "gla_display_destroy",
    "gla_make_current", "gla_os3_present_init", "gla_os3_virgl_transport", "gla_swap",
};

/* A session as mglw.h has it, with this file's own fields. */
struct session {
    struct mglw_session s;
    APTR gla[MGLW_G_COUNT];
    struct gla_virgl_transport transport;
    APTR fn[MGLW_SLOTS];
    struct StackSwapStruct stack;
};

static struct SignalSemaphore lock;
static struct MinList sessions;

void mglw_sessions_init(void)
{
    InitSemaphore(&lock);
    sessions.mlh_Head = (struct MinNode *)&sessions.mlh_Tail;
    sessions.mlh_Tail = NULL;
    sessions.mlh_TailPred = (struct MinNode *)&sessions.mlh_Head;
}

static int str_cmp(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return (int)(UBYTE)*a - (int)(UBYTE)*b;
}

/* The module's exports are in name order. */
static APTR find_export(const struct GLModuleTable *m, const char *name)
{
    ULONG lo = 0, hi = m->count;
    while (lo < hi) {
        ULONG mid = (lo + hi) / 2;
        int c = str_cmp(name, m->exports[mid].name);
        if (c == 0) return m->exports[mid].func;
        if (c < 0) hi = mid;
        else lo = mid + 1;
    }
    return NULL;
}

ULONG mglw_gla(struct mglw_session *s, int which, ULONG n, ULONG a, ULONG b, ULONG c, ULONG d, ULONG e)
{
    APTR f = ((struct session *)s)->gla[which];
    if (!f) return 0;
    return mglw_invoke(s, f, n, a, b, c, d, e);
}

/* Runs through mglw_invoke, so on the GL stack: the module's start runs Mesa's constructors. */
static ULONG open_module(struct session *ss)
{
    struct GLModuleTable *m = NULL;
    ss->s.handle = OGPU_ModuleOpen((CONST_STRPTR)GL_MODULE_NAME, GL_MODULE_VERSION, (APTR *)&m);
    if (!ss->s.handle) return 0;
    ss->s.module = m;
    return 1;
}

static ULONG close_module(struct session *ss)
{
    ss->s.module->close();
    return 0;
}

static int want_cpu(void)
{
    char v[8];
    return GetVar((STRPTR)"MiniGL/Driver", (STRPTR)v, sizeof v, 0) > 0 && (v[0] == 'C' || v[0] == 'c');
}

static void session_free(struct session *ss)
{
    if (ss->s.display) mglw_gla(&ss->s, MGLW_G_DISPLAY_DESTROY, 1, (ULONG)ss->s.display, 0, 0, 0, 0);
    if (ss->s.module) mglw_invoke(&ss->s, (APTR)close_module, 1, ss);
    if (ss->s.handle) OGPU_ModuleClose(ss->s.handle);
    if (ss->s.stack_mem) FreeVec(ss->s.stack_mem);
    FreeVec(ss);
}

static struct session *session_make(struct Task *me)
{
    struct session *ss;
    ULONG i;
    if (me->tc_Node.ln_Type != NT_PROCESS) return NULL;    /* a module comes from disk */
    if (!(ss = AllocVec(sizeof *ss, MEMF_ANY | MEMF_CLEAR))) return NULL;
    ss->s.fn = ss->fn;
    ss->s.owner = me;
    if ((ULONG)me->tc_SPUpper - (ULONG)me->tc_SPLower < GL_STACK_MIN &&
        (ss->s.stack_mem = AllocVec(GL_STACK_SIZE, MEMF_ANY))) {
        ss->stack.stk_Lower = ss->s.stack_mem;
        ss->stack.stk_Upper = (ULONG)ss->s.stack_mem + GL_STACK_SIZE;
        ss->stack.stk_Pointer = (APTR)ss->stack.stk_Upper;
        ss->s.sss = &ss->stack;
        ss->s.task = me;
    }
    if (!mglw_invoke(&ss->s, (APTR)open_module, 1, ss)) {  /* no GL.module, or one too old */
        session_free(ss);
        return NULL;
    }
    ss->s.module->head.caller_a4 = NULL;                    /* nothing of ours needs A4 */
    ss->s.a4 = ss->s.module->head.a4;
    for (i = 0; i < MGLW_G_COUNT; i++)
        ss->gla[i] = find_export(ss->s.module, gla_names[i]);
    for (i = 0; i < MGLW_SLOTS; i++) {
        APTR f = find_export(ss->s.module, mglw_gl_names[i]);
        ss->fn[i] = f ? f : (APTR)mglw_missing;
    }
    if (!want_cpu() && mglw_gla(&ss->s, MGLW_G_OS3_VIRGL_TRANSPORT, 1, (ULONG)&ss->transport, 0, 0, 0, 0)) {
        ss->s.display = (struct gla_display *)mglw_gla(&ss->s, MGLW_G_DISPLAY_CREATE_VIRGL, 1,
                                                       (ULONG)&ss->transport, 0, 0, 0, 0);
        ss->s.virgl = ss->s.display != NULL;
    }
    if (!ss->s.display)
        ss->s.display = (struct gla_display *)mglw_gla(&ss->s, MGLW_G_DISPLAY_CREATE, 0, 0, 0, 0, 0, 0);
    if (!ss->s.display) {
        session_free(ss);
        return NULL;
    }
    return ss;
}

struct mglw_session *mglw_session_get(void)
{
    struct Task *me = FindTask(NULL);
    struct MinNode *n;
    struct session *ss = NULL;
    ObtainSemaphore(&lock);
    for (n = sessions.mlh_Head; n->mln_Succ; n = n->mln_Succ) {
        struct session *t = (struct session *)((UBYTE *)n - offsetof(struct session, s.node));
        if (t->s.owner == me) { ss = t; break; }
    }
    if (!ss && (ss = session_make(me)))
        AddTail((struct List *)&sessions, (struct Node *)&ss->s.node);
    if (ss) ss->s.contexts++;
    ReleaseSemaphore(&lock);
    return ss ? &ss->s : NULL;
}

void mglw_session_release(struct mglw_session *s)
{
    ObtainSemaphore(&lock);
    if (--s->contexts == 0) {
        Remove((struct Node *)&s->node);
        session_free((struct session *)s);
    }
    ReleaseSemaphore(&lock);
}
