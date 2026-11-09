/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library 30: the library itself (clean-room, rebuilt 9 November
 * 2026 from docs/design/MiniGL-Compatible-Spec.md sections 3-5; see
 * PROVENANCE.md). The four standard vectors and one more, at -30, which
 * answers the dispatch table (mgl_table.c) in D0 and A0. The table points at
 * mgl_current, the one current-context variable, which programs write and
 * every entry reads.
 *
 * The front end is built twice (build.sh): for the FPU and soft. The first
 * open picks the table of the half this machine can run (an FPU in
 * AttnFlags, and ENV:MINIGL_FORCE_SOFT not set) and keeps it.
 *
 * Also here: the libraries the rest uses, ENV:MiniGL/ preferences, the
 * mglChoose* and mglProhibit* requests' defaults (spec 9.2), memory, the
 * clock and tracing. Built bare: no startup code, no C library. */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/timer.h>
#include <stdarg.h>

#include "mgl_internal.h"
#include "../../include/gfopengpu/gfopengpu.h"
#include "../../include/proto/gfopengpu.h"

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
struct Library *CyberGfxBase;
struct Library *UtilityBase;
struct Library *OpenGPUBase;
struct Device *TimerBase;
/* The soft half's libm (libnix, libm020) does its float work through these. */
struct Library *MathIeeeSingBasBase, *MathIeeeSingTransBase, *MathIeeeDoubBasBase, *MathIeeeDoubTransBase;

GLcontext mgl_current;                  /* the current context (table offset 16) */
struct mgl_prefs mgl_prefs;
long mgl_trace;

extern const MGLDispatchTable mgl_table_fpu, mgl_table_soft;
const MGLDispatchTable *mgl_handed;     /* the table -30 answers */

struct MGLBase {
    struct Library lib;
    BPTR seglist;
    struct timerequest treq;
    int timer_open;
};

/* Run as a program, the library does nothing. This must stay the first code
 * in the file: build.sh keeps source order (-fno-toplevel-reorder). */
int start(void) { return -1; }

#define MGL_ID "minigl.library 30.0 (9.11.2026) AmiCradle GL 1.1 for minigl.library programs, AmiCradle Team"
static const char lib_name[] = "minigl.library";
static const char lib_id[] = MGL_ID "\r\n";
static const char lib_ver[] __attribute__((used)) = "\0$VER: " MGL_ID;

static struct Library *lib_init(REG(d0, struct MGLBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct MGLBase *base));
static BPTR lib_close(REG(a6, struct MGLBase *base));
static BPTR lib_expunge(REG(a6, struct MGLBase *base));
static ULONG lib_null(void) { return 0; }

/* -30: the table, in D0 and in A0 (spec 3: a compiler might read either).
 * Nothing but A6 is set up by the caller, and nothing else is touched. */
void mgl_get_table(void);
__asm__(".text\n"
        "    .even\n"
        "_mgl_get_table:\n"
        "    move.l _mgl_handed,d0\n"
        "    move.l d0,a0\n"
        "    rts\n");

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)mgl_get_table,
    (APTR)-1,
};
static const struct { ULONG size; const APTR *vectors; APTR data; APTR init; } lib_inittable = {
    sizeof(struct MGLBase), lib_vectors, NULL, (APTR)lib_init,
};
const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, MGL_LIB_VERSION, NT_LIBRARY, 0, (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable,
};

/* ---- the library -------------------------------------------------------------------------- */

static struct Library *lib_init(REG(d0, struct MGLBase *base), REG(a0, BPTR seglist), REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Revision = MGL_LIB_REVISION;
    mgl_prefs_defaults();
    return &base->lib;
}

static int env_set(const char *name)
{
    char v[8];
    return GetVar((STRPTR)name, (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) > 0;
}

/* The libraries, the clock and the table: on the first open, in the
 * opener's task (exec's OpenLibrary loads disk libraries). */
static int open_libs(struct MGLBase *base)
{
    if (!DOSBase) DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 39);
    if (!GfxBase) GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!IntuitionBase) IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    if (!UtilityBase) UtilityBase = OpenLibrary((CONST_STRPTR)"utility.library", 39);
    if (!CyberGfxBase) CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 40);
    if (!MathIeeeSingBasBase) MathIeeeSingBasBase = OpenLibrary((CONST_STRPTR)"mathieeesingbas.library", 37);
    if (!MathIeeeSingTransBase) MathIeeeSingTransBase = OpenLibrary((CONST_STRPTR)"mathieeesingtrans.library", 37);
    if (!MathIeeeDoubBasBase) MathIeeeDoubBasBase = OpenLibrary((CONST_STRPTR)"mathieeedoubbas.library", 37);
    if (!MathIeeeDoubTransBase) MathIeeeDoubTransBase = OpenLibrary((CONST_STRPTR)"mathieeedoubtrans.library", 37);
    if (!DOSBase || !GfxBase || !IntuitionBase || !UtilityBase || !CyberGfxBase) return 0;
    if (!MathIeeeSingBasBase || !MathIeeeSingTransBase || !MathIeeeDoubBasBase || !MathIeeeDoubTransBase) return 0;
    if (!base->timer_open) {
        if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, &base->treq.tr_node, 0) != 0) return 0;
        TimerBase = base->treq.tr_node.io_Device;
        base->timer_open = 1;
    }
    if (!OpenGPUBase) {
        OpenGPUBase = OpenLibrary((CONST_STRPTR)"gfopengpu.library", 0);
        mgl_prefs.gpu_3d = mgl_prefs.gpu_host = 0;
        if (OpenGPUBase) {
            ULONG a = OGPU_Query(OGPU_OP_TRIANGLES, OGPU_FMT_ARGB32);
            if (OGPU_ANSWER(a) != OGPU_NONE) {
                mgl_prefs.gpu_3d = 1;
                mgl_prefs.gpu_host = OGPU_BACKEND(a) != OGPU_BACKEND_CPU;
            }
        }
    }
    if (!mgl_handed) {
        int fpu = (SysBase->AttnFlags & (AFF_68881 | AFF_68882 | AFF_FPU40)) != 0;
        mgl_handed = fpu && !env_set("MINIGL_FORCE_SOFT") ? &mgl_table_fpu : &mgl_table_soft;
    }
    return 1;
}

static struct Library *lib_open(REG(a6, struct MGLBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    if (!open_libs(base)) { base->lib.lib_OpenCnt--; return 0; }
    /* A new client starts from the defaults (spec 3, resident state). */
    mgl_prefs_defaults();
    mgl_read_prefs();
    return &base->lib;
}

static BPTR lib_close(REG(a6, struct MGLBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static void close_lib(struct Library **b)
{
    if (*b) CloseLibrary(*b);
    *b = 0;
}

static BPTR lib_expunge(REG(a6, struct MGLBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) { base->lib.lib_Flags |= LIBF_DELEXP; return 0; }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    if (base->timer_open) CloseDevice(&base->treq.tr_node);
    TimerBase = 0;
    close_lib(&OpenGPUBase);
    close_lib(&CyberGfxBase);
    close_lib(&UtilityBase);
    close_lib((struct Library **)&IntuitionBase);
    close_lib((struct Library **)&GfxBase);
    close_lib(&MathIeeeDoubTransBase);
    close_lib(&MathIeeeDoubBasBase);
    close_lib(&MathIeeeSingTransBase);
    close_lib(&MathIeeeSingBasBase);
    close_lib((struct Library **)&DOSBase);
    mgl_handed = 0;
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

/* ---- preferences ---------------------------------------------------------------------------- */

/* ENV:MiniGL/ (read at each open and each new context):
 *   Driver          Auto, CPU, GFOpenGPU or Virgl: where triangles are drawn
 *                   (Virgl: the host's GPU through virgl, else as Auto)
 *   Flip            1: a screen changes buffers instead of blitting the frame
 *   MaxTextureSize  what glGetIntegerv(GL_MAX_TEXTURE_SIZE) answers (default 1024)
 *   Trace           n: n lines of trace to the serial port */
static LONG env_long(const char *name, LONG dflt)
{
    char v[16];
    LONG n;
    if (GetVar((STRPTR)name, (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) <= 0) return dflt;
    return StrToLong((STRPTR)v, &n) > 0 ? n : dflt;
}

void mgl_read_prefs(void)
{
    char v[16];
    mgl_prefs.route = -1;
    if (GetVar((STRPTR)"MiniGL/Driver", (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) > 0) {
        if (v[0] == 'C' || v[0] == 'c') mgl_prefs.route = ROUTE_CPU;
        else if (v[0] == 'V' || v[0] == 'v') mgl_prefs.route = ROUTE_VIRGL;
        else if (v[0] == 'G' || v[0] == 'g' || v[0] == 'O' || v[0] == 'o') mgl_prefs.route = ROUTE_GPU;
    }
    mgl_prefs.flip = env_long("MiniGL/Flip", 0) != 0;
    mgl_prefs.max_tex = env_long("MiniGL/MaxTextureSize", 1024);
    if (mgl_prefs.max_tex < 64 || mgl_prefs.max_tex > 2048) mgl_prefs.max_tex = 1024;
    mgl_trace = env_long("MiniGL/Trace", 0);
}

/* ---- memory, the clock, tracing ------------------------------------------------------------- */

void *mgl_alloc(ULONG bytes) { return AllocVec(bytes ? bytes : 4, MEMF_ANY | MEMF_CLEAR); }
void mgl_free(void *p) { if (p) FreeVec(p); }

void mgl_copy(void *d, const void *s, ULONG n)
{
    if (n) CopyMem((APTR)s, d, n);
}

void mgl_zero(void *d, ULONG n)
{
    UBYTE *b = d;
    while (n && ((ULONG)b & 3)) { *b++ = 0; n--; }
    {
        ULONG *l = (ULONG *)b;
        for (; n >= 4; n -= 4) *l++ = 0;
        b = (UBYTE *)l;
    }
    while (n--) *b++ = 0;
}

ULONG mgl_millis(void)
{
    struct timeval tv;
    if (!TimerBase) return 0;
    GetSysTime(&tv);
    return (ULONG)tv.tv_secs * 1000UL + (ULONG)tv.tv_micro / 1000UL;
}

/* A character to the serial port, straight to the chip: works in any task
 * and inside any lock. */
static void serput(REG(d0, UBYTE ch), REG(a3, void *data))
{
    volatile UWORD *serdatr = (volatile UWORD *)0xDFF018, *serdat = (volatile UWORD *)0xDFF030;
    long spin = 100000;
    (void)data;
    if (!ch) ch = '\n';
    while (!(*serdatr & 0x2000) && --spin) ;
    *serdat = (UWORD)(0x100 | ch);
}

/* One line ("%ld %lx %s", no floats) while ENV:MiniGL/Trace has lines left. */
void mgl_log(const char *fmt, ...)
{
    va_list ap;
    if (mgl_trace <= 0) return;
    mgl_trace--;
    va_start(ap, fmt);
    RawDoFmt((STRPTR)fmt, (APTR)ap, (void (*)())serput, 0);
    va_end(ap);
}
