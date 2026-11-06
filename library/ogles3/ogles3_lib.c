/* Copyright (c) 2026 Dalsin Limited / the AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ogles3.library on GFOpenGPU: OpenGL ES 3.1 (and 2.0) for programs built
 * for ladislawv's ogles3.library, drawn by GFOpenGPU's GL module: Mesa on
 * the PC's graphics chip through virgl on AmiCradle, softpipe on the 68k
 * otherwise. This library holds no GL of its own. It keeps ogles3's name,
 * its library vectors and its dispatch table (ABI 1, 380 entries,
 * ogles3_api.txt), so programs built with ogles3's SDK run unchanged:
 *   -30  const struct OGLES3Dispatch *OGLES3GetDispatchTable(void)
 *   -36  ULONG OGLES3PrivateLogFlush(void)   (nothing to flush here: 0)
 * The table's agl calls are here; its GL calls (ogles3_thunks.s,
 * ogles3_call.s) forward to GL.module's entry points of the same name.
 *
 * Each task that makes contexts gets a session: its own opening of
 * GL.module (OGPU_ModuleOpen), so its own copy of Mesa's data, a GLA display
 * (virgl when gfopengpu.library carries it, else softpipe; ENV:OGLES3/Driver
 * CPU takes softpipe), and the module's calls bound by name. Contexts made
 * with OGLES3_CCT_SHARE_WITH join the session of the one they share with.
 * Mesa runs on a 1 MB GL stack of the session's when the task's own is
 * under 256 KB, as libGL.a does for programs.
 *
 * The interface is ladislawv's (ogles3.library 1.0-beta2, MIT headers and
 * autodoc, https://github.com/ladislawv/ogles3); this implementation was
 * written by the Team from those documents only. Built bare by build.sh:
 * no startup code, no C library. Compiled -ffixed-a4: GL.module's calls set
 * A4 (include/gfopengpu/module.h), and nothing here uses it. */
#include <stddef.h>
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <exec/lists.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <graphics/displayinfo.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#include "../../include/gfopengpu/module.h"
#include "../../stubs/gl/gl_module.h"
#include "../modules/gl/gla/gla_core.h"
#include "../modules/gl/gla/os3/gla_present_os3.h"

#define OPENGPU_BASE_NAME OpenGPUBase
#define GFOPENGPU_BASE_NAME OpenGPUBase
#include <inline/gfopengpu.h>

#include "ogles3_face.h"

#define REG(r, decl) register decl __asm(#r)

#define OGL3_VERSION 1
#define OGL3_REVISION 1

/* ogles3's context tags and error codes (its <libraries/ogles3.h>). */
#define CCT_MIN             (TAG_USER + 0x4f4c0000UL)
#define CCT_WINDOW          (CCT_MIN + 1)
#define CCT_MODEID          (CCT_MIN + 2)
#define CCT_DEPTH           (CCT_MIN + 3)
#define CCT_STENCIL         (CCT_MIN + 4)
#define CCT_VSYNC           (CCT_MIN + 5)
#define CCT_GET_WIDTH       (CCT_MIN + 7)
#define CCT_GET_HEIGHT      (CCT_MIN + 8)
#define CCT_BITMAP          (CCT_MIN + 9)
#define CCT_CONTEXT_FOR_MODEID (CCT_MIN + 11)
#define CCT_SHARE_WITH      (CCT_MIN + 17)
#define CCT_PS_MIN          (CCT_MIN + 0x1000)
#define CCT_PS_STATE_ONLY   (CCT_PS_MIN + 1)
#define CCT_PS_WIDTH        (CCT_PS_MIN + 2)
#define CCT_PS_HEIGHT       (CCT_PS_MIN + 3)
#define CCT_PS_GET_WINDOW   (CCT_PS_MIN + 4)
#define CCT_PS_MSAA         (CCT_PS_MIN + 5)
#define CCT_PS_ES_VERSION   (CCT_PS_MIN + 6)
/* Accepted and not acted on, as ogles3 itself does with most of them:
 * SINGLE_GET_ERROR_MODE (+6), SHADER_COMPAT_PATCH (+10), RESIZE_VIEWPORT
 * (+12), DEBUG_SHADER_LOG (+13), the SPIRV tags (+14 to +16),
 * DETECT_UNINITIALIZED_GLSL_VARS (+18), and PISTORM_MSAA (GLA has no
 * multisampled default framebuffer yet). */

#define CCE_OK          0
#define CCE_NOMEM       1
#define CCE_NO_GPU      2
#define CCE_BAD_TAGS    3
#define CCE_NO_DISPLAY  4
#define CCE_UNSUPPORTED 5

#define NO_MODE 0xFFFFFFFFUL                /* graphics' INVALID_ID, unsigned */

#define GL_STACK_MIN  (256 * 1024)
#define GL_STACK_SIZE (1024 * 1024)

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
struct Library *UtilityBase;
struct Library *OpenGPUBase;

/* The GLA calls the library makes, bound by name in each session. */
enum {
    G_BUFFER_CREATE, G_BUFFER_DESTROY, G_BUFFER_RESIZE, G_CONTEXT_CREATE, G_CONTEXT_DESTROY,
    G_DISPLAY_CREATE, G_DISPLAY_CREATE_VIRGL, G_DISPLAY_DESTROY, G_DISPLAY_VERSION, G_MAKE_CURRENT,
    G_OS3_PRESENT_INIT, G_OS3_VIRGL_TRANSPORT, G_SWAP, G_COUNT
};
static const char *const gla_names[G_COUNT] = {
    "gla_buffer_create", "gla_buffer_destroy", "gla_buffer_resize", "gla_context_create", "gla_context_destroy",
    "gla_display_create", "gla_display_create_virgl", "gla_display_destroy", "gla_display_version", "gla_make_current",
    "gla_os3_present_init", "gla_os3_virgl_transport", "gla_swap",
};

/* A session as ogles3_face.h has it, with the library's own fields. */
struct session {
    struct ogl3_session s;
    APTR gla[G_COUNT];
    struct gla_virgl_transport transport;
    APTR fn[OGL3_SLOTS];
    struct StackSwapStruct stack;
};

struct ogl3_context {
    struct ogl3_session *s;             /* first: ogles3_call.s reads it */
    struct gla_context *c;
    struct gla_buffer *b;
    struct gla_config cfg;
    struct gla_present hook;            /* read by GL.module while the buffer lives */
    struct gla_os3_target target;
    struct RastPort bmrp;               /* OGLES3_CCT_BITMAP's */
    struct Window *win;                 /* what it draws into (the program's, or its own) */
    struct Window *own_win;
    struct Screen *own_scr;
    ULONG w, h;
    ULONG vsync;
    ULONG es;                           /* 20 or 31 */
};

struct OGL3Base {
    struct Library lib;
    BPTR seglist;
};

static struct SignalSemaphore lock;
static struct MinList sessions;

/* Run as a program, the library does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

static const char lib_name[] = "ogles3.library";
static const char lib_id[] = "ogles3.library 1.1 (6.11.2026) on GFOpenGPU, Dalsin Limited / the AmiCradle Team\r\n";
static const char lib_ver[] __attribute__((used)) =
    "\0$VER: ogles3.library 1.1 (6.11.2026) on GFOpenGPU, Dalsin Limited / the AmiCradle Team";

static struct Library *lib_init(REG(d0, struct OGL3Base *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct OGL3Base *base));
static BPTR lib_close(REG(a6, struct OGL3Base *base));
static BPTR lib_expunge(REG(a6, struct OGL3Base *base));
static ULONG lib_null(void) { return 0; }
static const struct ogl3_dispatch *lib_dispatch(REG(a6, struct OGL3Base *base)) { (void)base; return &ogl3_dispatch; }
static ULONG lib_logflush(REG(a6, struct OGL3Base *base)) { (void)base; return 0; }

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)lib_dispatch,             /* -30: the dispatch table */
    (APTR)lib_logflush,             /* -36: ogles3's private log flush */
    (APTR)-1,
};

static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct OGL3Base), lib_vectors, NULL, (APTR)lib_init,
};

const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, OGL3_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

static struct Library *lib_init(REG(d0, struct OGL3Base *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Revision = OGL3_REVISION;
    InitSemaphore(&lock);
    sessions.mlh_Head = (struct MinNode *)&sessions.mlh_Tail;
    sessions.mlh_Tail = NULL;
    sessions.mlh_TailPred = (struct MinNode *)&sessions.mlh_Head;
    return &base->lib;
}

static int open_libs(void)
{
    if (!DOSBase) DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 39);
    if (!GfxBase) GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!IntuitionBase) IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    if (!UtilityBase) UtilityBase = OpenLibrary((CONST_STRPTR)"utility.library", 39);
    /* GL comes from GFOpenGPU: without gfopengpu.library (0.5 or later, for
     * OGPU_ModuleOpen) this library won't open, and a program's
     * OGLES3Open goes on to the next ogles3.library it knows. */
    if (!OpenGPUBase) {
        OpenGPUBase = OpenLibrary((CONST_STRPTR)"gfopengpu.library", 0);
        if (OpenGPUBase && OpenGPUBase->lib_Version == 0 && OpenGPUBase->lib_Revision < OGPU_MODULE_LIB_REVISION) {
            CloseLibrary(OpenGPUBase);
            OpenGPUBase = NULL;
        }
    }
    return DOSBase && GfxBase && IntuitionBase && UtilityBase && OpenGPUBase;
}

static struct Library *lib_open(REG(a6, struct OGL3Base *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    if (!open_libs()) {
        base->lib.lib_OpenCnt--;
        return NULL;
    }
    return &base->lib;
}

static BPTR lib_close(REG(a6, struct OGL3Base *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct OGL3Base *base))
{
    BPTR seglist;
    /* A session still open (a program that ended without destroying its
     * contexts) holds GL.module's code: the library stays. */
    if (base->lib.lib_OpenCnt || sessions.mlh_Head->mln_Succ) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    if (OpenGPUBase) CloseLibrary(OpenGPUBase);
    if (UtilityBase) CloseLibrary(UtilityBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (DOSBase) CloseLibrary((struct Library *)DOSBase);
    OpenGPUBase = UtilityBase = NULL;
    IntuitionBase = NULL;
    GfxBase = NULL;
    DOSBase = NULL;
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

/* ---- names ----------------------------------------------------------------------------- */

static int str_cmp(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return (int)(UBYTE)*a - (int)(UBYTE)*b;
}

static ULONG str_len(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (ULONG)(p - s);
}

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

/* A GL call by name; an extension's name the module lacks (glFooOES) falls
 * back to the core call it became (glFoo), which takes the same arguments. */
static APTR find_gl(const struct GLModuleTable *m, const char *name)
{
    static const char *const suffix[] = { "OES", "EXT", "KHR", "ANGLE", "NV", "APPLE" };
    char base[80];
    APTR f = find_export(m, name);
    ULONG n = str_len(name), i, k;
    if (f || n >= sizeof base) return f;
    for (i = 0; i < sizeof suffix / sizeof suffix[0]; i++) {
        ULONG sl = str_len(suffix[i]);
        if (n <= sl + 2 || str_cmp(name + n - sl, suffix[i])) continue;
        for (k = 0; k < n - sl; k++) base[k] = name[k];
        base[k] = 0;
        return find_export(m, base);
    }
    return NULL;
}

/* ---- sessions -------------------------------------------------------------------------- */

static ULONG gla_call(struct ogl3_session *s, int which, ULONG n, ULONG a, ULONG b, ULONG c, ULONG d, ULONG e)
{
    APTR f = ((struct session *)s)->gla[which];
    if (!f) return 0;
    return ogl3_invoke(s, f, n, a, b, c, d, e);
}
#define GLA0(s, w) gla_call((s), (w), 0, 0, 0, 0, 0, 0)
#define GLA1(s, w, a) gla_call((s), (w), 1, (ULONG)(a), 0, 0, 0, 0)
#define GLA2(s, w, a, b) gla_call((s), (w), 2, (ULONG)(a), (ULONG)(b), 0, 0, 0)
#define GLA3(s, w, a, b, c) gla_call((s), (w), 3, (ULONG)(a), (ULONG)(b), (ULONG)(c), 0, 0)
#define GLA5(s, w, a, b, c, d, e) gla_call((s), (w), 5, (ULONG)(a), (ULONG)(b), (ULONG)(c), (ULONG)(d), (ULONG)(e))

/* Runs through ogl3_invoke, so on the GL stack: the module's start runs
 * Mesa's constructors. */
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
    return GetVar((STRPTR)"OGLES3/Driver", (STRPTR)v, sizeof v, 0) > 0 && (v[0] == 'C' || v[0] == 'c');
}

static void session_free(struct session *ss)
{
    if (ss->s.display) GLA1(&ss->s, G_DISPLAY_DESTROY, ss->s.display);
    if (ss->s.module) ogl3_invoke(&ss->s, (APTR)close_module, 1, ss);
    if (ss->s.handle) OGPU_ModuleClose(ss->s.handle);
    if (ss->s.stack_mem) FreeVec(ss->s.stack_mem);
    FreeVec(ss);
}

/* The calling task's session, made when it has none: NULL and *err set on
 * failure. Called with the lock held. */
static struct session *session_get(ULONG *err)
{
    struct Task *me = FindTask(NULL);
    struct MinNode *n;
    struct session *ss;
    ULONG i;
    for (n = sessions.mlh_Head; n->mln_Succ; n = n->mln_Succ) {
        ss = (struct session *)((UBYTE *)n - offsetof(struct session, s.node));
        if (ss->s.owner == me) return ss;
    }
    if (me->tc_Node.ln_Type != NT_PROCESS) {   /* a module comes from disk */
        *err = CCE_NO_GPU;
        return NULL;
    }
    if (!(ss = AllocVec(sizeof *ss, MEMF_ANY | MEMF_CLEAR))) {
        *err = CCE_NOMEM;
        return NULL;
    }
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
    if (!ogl3_invoke(&ss->s, (APTR)open_module, 1, ss)) {
        *err = CCE_NO_GPU;                      /* no GL.module, or one too old */
        session_free(ss);
        return NULL;
    }
    ss->s.module->head.caller_a4 = NULL;        /* nothing of ours needs A4 */
    ss->s.a4 = ss->s.module->head.a4;
    for (i = 0; i < G_COUNT; i++)
        ss->gla[i] = find_export(ss->s.module, gla_names[i]);
    for (i = 0; i < OGL3_SLOTS; i++) {
        APTR f = ogl3_gl_names[i] ? find_gl(ss->s.module, ogl3_gl_names[i]) : NULL;
        ss->fn[i] = f ? f : (APTR)ogl3_missing;
    }
    if (!want_cpu() && GLA1(&ss->s, G_OS3_VIRGL_TRANSPORT, &ss->transport)) {
        ss->s.display = (struct gla_display *)GLA1(&ss->s, G_DISPLAY_CREATE_VIRGL, &ss->transport);
        ss->s.virgl = ss->s.display != NULL;
    }
    if (!ss->s.display) ss->s.display = (struct gla_display *)GLA0(&ss->s, G_DISPLAY_CREATE);
    if (!ss->s.display) {
        *err = CCE_NO_GPU;
        session_free(ss);
        return NULL;
    }
    AddTail((struct List *)&sessions, (struct Node *)&ss->s.node);
    return ss;
}

static void session_release(struct ogl3_session *s)
{
    ObtainSemaphore(&lock);
    if (--s->contexts == 0) {
        Remove((struct Node *)&s->node);
        session_free((struct session *)s);
    }
    ReleaseSemaphore(&lock);
}

/* ---- contexts -------------------------------------------------------------------------- */

void ogl3_switch(struct ogl3_context *c)
{
    struct ogl3_session *s = c->s;
    if (GLA2(s, G_MAKE_CURRENT, c->c, c->b)) s->current = c;
}

static void inner_size(struct Window *w, ULONG *wd, ULONG *ht)
{
    LONG x = w->Width - w->BorderLeft - w->BorderRight, y = w->Height - w->BorderTop - w->BorderBottom;
    *wd = x > 0 ? (ULONG)x : 1;
    *ht = y > 0 ? (ULONG)y : 1;
}

static void target_window(struct ogl3_context *c, struct Window *w)
{
    c->win = w;
    c->target.rp = w->RPort;
    c->target.bitmap = NULL;
    c->target.left = w->BorderLeft;
    c->target.top = w->BorderTop;
    inner_size(w, &c->w, &c->h);
}

static void target_bitmap(struct ogl3_context *c, struct BitMap *bm)
{
    InitRastPort(&c->bmrp);
    c->bmrp.BitMap = bm;
    c->win = NULL;
    c->target.rp = &c->bmrp;
    c->target.bitmap = NULL;
    c->target.left = 0;
    c->target.top = 0;
    c->w = GetBitMapAttr(bm, BMA_WIDTH);
    c->h = GetBitMapAttr(bm, BMA_HEIGHT);
}

/* A full screen of its own in mode id, with a backdrop window for input. */
static int open_screen(struct ogl3_context *c, ULONG id, ULONG w, ULONG h)
{
    struct DimensionInfo dims;
    ULONG depth;
    if (!GetDisplayInfoData(NULL, (UBYTE *)&dims, sizeof dims, DTAG_DIMS, id)) return 0;
    depth = dims.MaxDepth;
    if (depth < 15) return 0;
    if (!w) w = dims.Nominal.MaxX - dims.Nominal.MinX + 1;
    if (!h) h = dims.Nominal.MaxY - dims.Nominal.MinY + 1;
    c->own_scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, w, SA_Height, h, SA_Depth, depth, SA_Quiet, TRUE,
                                SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN, SA_Draggable, FALSE,
                                SA_Title, (ULONG) "ogles3", TAG_DONE);
    if (!c->own_scr) return 0;
    c->own_win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)c->own_scr, WA_Left, 0, WA_Top, 0,
                                WA_Width, c->own_scr->Width, WA_Height, c->own_scr->Height, WA_Backdrop, TRUE,
                                WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE, WA_NoCareRefresh, TRUE,
                                WA_SimpleRefresh, TRUE, TAG_DONE);
    if (!c->own_win) return 0;
    target_window(c, c->own_win);
    c->target.bitmap = c->own_scr->RastPort.BitMap;    /* a screen GL owns: GFOpenGPU draws into it */
    return 1;
}

static int open_window(struct ogl3_context *c, ULONG w, ULONG h)
{
    c->own_win = OpenWindowTags(NULL, WA_InnerWidth, w ? w : 640, WA_InnerHeight, h ? h : 480,
                                WA_Title, (ULONG) "ogles3.library", WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                                WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_NoCareRefresh, TRUE, TAG_DONE);
    if (!c->own_win) return 0;
    target_window(c, c->own_win);
    return 1;
}

static void context_free(struct ogl3_context *c)
{
    struct ogl3_session *s = c->s;
    if (s) {
        if (s->current == c) {
            GLA2(s, G_MAKE_CURRENT, NULL, NULL);
            s->current = NULL;
        }
        if (c->b) GLA1(s, G_BUFFER_DESTROY, c->b);
        if (c->c) GLA1(s, G_CONTEXT_DESTROY, c->c);
    }
    if (c->own_win) CloseWindow(c->own_win);
    if (c->own_scr) CloseScreen(c->own_scr);
    FreeVec(c);
    if (s) session_release(s);
}

static void put_outputs(struct ogl3_context *c, struct TagItem *tags)
{
    struct TagItem *t, *state = tags;
    while ((t = NextTagItem(&state))) {
        if (t->ti_Tag == CCT_GET_WIDTH && t->ti_Data) *(ULONG *)t->ti_Data = c->w;
        else if (t->ti_Tag == CCT_GET_HEIGHT && t->ti_Data) *(ULONG *)t->ti_Data = c->h;
        else if (t->ti_Tag == CCT_PS_GET_WINDOW && t->ti_Data) *(struct Window **)t->ti_Data = c->win;
    }
}

struct ogl3_context *ogl3_aglCreateContext2(ULONG *errcode, struct TagItem *tags)
{
    struct TagItem *t, *state = tags;
    struct Window *win = NULL;
    struct BitMap *bm = NULL;
    struct ogl3_context *c, *share = NULL;
    struct session *ss;
    ULONG modeid = NO_MODE, w = 0, h = 0, depth = 0, stencil = 0, es = 31, err = CCE_OK;
    int state_only = 0, ok;

    if (!(c = AllocVec(sizeof *c, MEMF_ANY | MEMF_CLEAR))) {
        if (errcode) *errcode = CCE_NOMEM;
        return NULL;
    }
    while ((t = NextTagItem(&state))) {
        switch (t->ti_Tag) {
        case CCT_WINDOW: win = (struct Window *)t->ti_Data; break;
        case CCT_MODEID:
        case CCT_CONTEXT_FOR_MODEID: modeid = t->ti_Data; break;
        case CCT_BITMAP: bm = (struct BitMap *)t->ti_Data; break;
        case CCT_DEPTH: depth = t->ti_Data; break;
        case CCT_STENCIL: stencil = t->ti_Data; break;
        case CCT_VSYNC: c->vsync = t->ti_Data != 0; break;
        case CCT_SHARE_WITH: share = (struct ogl3_context *)t->ti_Data; break;
        case CCT_PS_STATE_ONLY: state_only = t->ti_Data != 0; break;
        case CCT_PS_WIDTH: w = t->ti_Data; break;
        case CCT_PS_HEIGHT: h = t->ti_Data; break;
        case CCT_PS_ES_VERSION:
            if (t->ti_Data == 20) es = 20;
            else if (t->ti_Data == 30 || t->ti_Data == 31) es = 31;
            else if (t->ti_Data == 32) err = CCE_UNSUPPORTED;
            else err = CCE_BAD_TAGS;
            break;
        default: break;
        }
    }
    if (!err && ((win != NULL) + (bm != NULL) + (modeid != NO_MODE)) > 1) err = CCE_BAD_TAGS;
    if (!err && share && share->es != es) err = CCE_BAD_TAGS;
    if (err) {
        FreeVec(c);
        if (errcode) *errcode = err;
        return NULL;
    }
    c->es = es;
    c->cfg.profile = GLA_ES2;
    c->cfg.major = es == 20 ? 2 : 3;
    c->cfg.minor = es == 20 ? 0 : 1;
    c->cfg.doublebuf = 1;
    c->cfg.depth = stencil ? 24 : depth == 0 ? 0 : depth <= 16 ? 16 : 24;
    c->cfg.stencil = stencil ? 8 : 0;
    c->cfg.alpha = 0;

    ObtainSemaphore(&lock);
    if (share) {
        ss = (struct session *)share->s;
    } else {
        ss = session_get(&err);
    }
    if (ss) {
        ss->s.contexts++;
        c->s = &ss->s;
    }
    ReleaseSemaphore(&lock);
    if (!ss) {
        FreeVec(c);
        if (errcode) *errcode = err;
        return NULL;
    }
    if ((ULONG)GLA2(c->s, G_DISPLAY_VERSION, c->s->display, GLA_ES2) < es) {
        context_free(c);
        if (errcode) *errcode = CCE_UNSUPPORTED;
        return NULL;
    }

    if (state_only) {
        c->w = w ? w : 1;
        c->h = h ? h : 1;
        ok = 1;
    } else if (win) {
        target_window(c, win);
        ok = 1;
    } else if (bm) {
        target_bitmap(c, bm);
        ok = 1;
    } else if (modeid != NO_MODE) {
        ok = open_screen(c, modeid, w, h);
    } else {
        ok = open_window(c, w, h);
    }
    if (!ok) {
        context_free(c);
        if (errcode) *errcode = CCE_NO_DISPLAY;
        return NULL;
    }
    GLA2(c->s, G_OS3_PRESENT_INIT, &c->hook, &c->target);
    c->c = (struct gla_context *)GLA3(c->s, G_CONTEXT_CREATE, c->s->display, &c->cfg, share ? share->c : NULL);
    if (c->c)
        c->b = (struct gla_buffer *)GLA5(c->s, G_BUFFER_CREATE, c->s->display, &c->cfg, c->w, c->h, &c->hook);
    if (!c->c || !c->b) {
        context_free(c);
        if (errcode) *errcode = CCE_NOMEM;
        return NULL;
    }
    put_outputs(c, tags);
    if (errcode) *errcode = CCE_OK;
    return c;
}

void ogl3_aglDestroyContext(struct ogl3_context *c)
{
    if (c) context_free(c);
}

void ogl3_aglMakeCurrent(struct ogl3_context *c)
{
    struct MinNode *n;
    struct Task *me;
    if (c) {
        if (c->s->current != c) ogl3_switch(c);
        return;
    }
    /* No context: let go of this task's current one. */
    me = FindTask(NULL);
    ObtainSemaphore(&lock);
    for (n = sessions.mlh_Head; n->mln_Succ; n = n->mln_Succ) {
        struct ogl3_session *s = (struct ogl3_session *)((UBYTE *)n - offsetof(struct ogl3_session, node));
        if (s->owner == me && s->current) {
            GLA2(s, G_MAKE_CURRENT, NULL, NULL);
            s->current = NULL;
        }
    }
    ReleaseSemaphore(&lock);
}

void ogl3_aglSwapBuffers(struct ogl3_context *c)
{
    if (!c || !c->b) return;
    if (c->s->current != c) ogl3_switch(c);
    if (c->vsync) WaitTOF();
    GLA2(c->s, G_SWAP, c->c, c->b);
}

static void resize(struct ogl3_context *c, ULONG ow, ULONG oh)
{
    if (c->b && (c->w != ow || c->h != oh)) GLA3(c->s, G_BUFFER_RESIZE, c->b, c->w, c->h);
}

void ogl3_aglSetParams2(struct ogl3_context *c, struct TagItem *tags)
{
    struct TagItem *t, *state = tags;
    if (!c) return;
    while ((t = NextTagItem(&state))) {
        if (t->ti_Tag == CCT_WINDOW && t->ti_Data && !c->own_win && c->target.rp != &c->bmrp) {
            ULONG ow = c->w, oh = c->h;
            target_window(c, (struct Window *)t->ti_Data);
            resize(c, ow, oh);
        } else if (t->ti_Tag == CCT_VSYNC) {
            c->vsync = t->ti_Data != 0;
        }
    }
    put_outputs(c, tags);
}

void ogl3_aglSetBitmap(struct ogl3_context *c, struct BitMap *bm)
{
    ULONG ow, oh;
    if (!c || !bm || c->target.rp != &c->bmrp) return;
    ow = c->w;
    oh = c->h;
    target_bitmap(c, bm);
    resize(c, ow, oh);
}
