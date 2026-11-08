/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * DEVS:Monitors/AGA and DEVS:Monitors/ECS: the chipset as a graphics card,
 * with a monitor of its own that holds its modes. Built twice from here
 * (-DCHIPSET_AGA, -DCHIPSET_ECS), and run by LoadMonDrvs with the others.
 *
 * ModeIDs stay as they are: programs hard-code them, and their top half names
 * the timing, not the chipset. What the monitor does:
 *
 *   - On a machine whose chipset can't show them, it does nothing.
 *   - It brings in the timings beyond PAL and NTSC. On GFOpenGfx's
 *     graphics.library (47.100 and up) the modes are built in and appear while a
 *     MonitorSpec of the right name is on GfxBase->MonitorList, so this adds it,
 *     with the values OS 3.2.3's drivers have (measured: gfopengfx
 *     tests/m68k/display_os323_screens.txt). GFOpenGfx has no records for the
 *     others (Euro36, Euro72, Super72). On OS 3.2's own graphics.library the
 *     machine's drivers in SYS:Storage/Monitors are run instead.
 *   - Each native mode's name says its chipset: "AGA PAL:High Res".
 *   - The ECS monitor on an AGA machine is a filtered view: DblNTSC and DblPAL,
 *     which ECS can't show, are left out of the list, and each mode's largest
 *     depth is ECS's. With the AGA monitor running, the ECS monitor does nothing.
 *
 * It stays in memory for its patches: GetDisplayInfoData and, for ECS,
 * NextDisplayInfo, by SetFunction, with every call passed on.
 */
/* No start-up code: the file's first instruction is where it starts (-fno-toplevel-reorder). */
__asm__(".text\n\tjmp _start\n");

#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <graphics/displayinfo.h>
#include <graphics/gfxbase.h>
#include <graphics/gfxnodes.h>
#include <graphics/modeid.h>
#include <graphics/monitor.h>
#include <hardware/custom.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>

#ifdef CHIPSET_AGA
#define CHIP_NAME "AGA"
#define MARKER "AGA.monitor"
#define NEEDS (GFXF_AA_ALICE | GFXF_AA_LISA)
#else
#define CHIP_NAME "ECS"
#define MARKER "ECS.monitor"
#define NEEDS GFXF_HR_AGNUS
#endif

#define REG(r, decl) register decl __asm(#r)
#define NATIVE(id) (((id) >> 24) == 0)              /* RTG's are 0x4n to 0x6n */
#define MONITOR_OF(id) ((id) & MONITOR_ID_MASK)
#define NAME_ROOM 32                                /* DISPLAYNAMELEN */

static const char version[] __attribute__((used)) = "$VER: " CHIP_NAME " 1.0 (8.11.2026) AmiCradle Team";

struct ExecBase *SysBase;
struct GfxBase *GfxBase;
struct DosLibrary *DOSBase;

/* As OS 3.2.3's drivers leave them (measured). The intervals are in colour
 * clocks from the top of the frame or the start of the line. */
struct timing {
    const char *name;
    UWORD ratioh, ratiov, rows, cclks, dmax, dmin, minrow, vb_stop, vs_start, vs_stop;
    WORD left, top, right, bottom;
};
static const struct timing timings[] = {
    { "multiscan.monitor", 8, 29, 505, 121, 245, 65, 25, 0x0BD1, 0x03C8, 0x0790, 81, 25, 85, 25 },
#ifdef CHIPSET_AGA
    { "DblNTSC.monitor", 9, 28, 477, 129, 261, 65, 23, 0x0B97, 0x0387, 0x070E, 81, 23, 101, 77 },
    { "DblPAL.monitor", 9, 28, 573, 129, 261, 65, 23, 0x0B97, 0x0387, 0x070E, 81, 23, 101, 61 },
#endif
};
#define TIMINGS (sizeof timings / sizeof timings[0])

/* OS 3.2's own drivers, by file, and the MonitorSpec each adds */
static const struct { const char *file, *monitor; } drivers[] = {
    { "SYS:Storage/Monitors/Multiscan", "multiscan.monitor" },
    { "SYS:Storage/Monitors/Euro36", "euro36.monitor" },
    { "SYS:Storage/Monitors/Euro72", "euro72.monitor" },
    { "SYS:Storage/Monitors/Super72", "super72.monitor" },
#ifdef CHIPSET_AGA
    { "SYS:Storage/Monitors/DblNTSC", "DblNTSC.monitor" },
    { "SYS:Storage/Monitors/DblPAL", "DblPAL.monitor" },
#endif
};

/* ---- the beam, when LoadView changes to this monitor --------------------- */

static LONG do_monitor(struct MonitorSpec *ms)
{
    volatile struct Custom *c = (struct Custom *)0xdff000;
    struct SpecialMonitor *s = ms->ms_Special;
    UWORD cclks = ms->total_colorclocks;
    c->htotal = cclks - 1;
    c->hbstrt = (UWORD)s->hblank.asi_Start;
    c->hbstop = (UWORD)s->hblank.asi_Stop;
    c->hsstrt = (UWORD)s->hsync.asi_Start;
    c->hsstop = (UWORD)s->hsync.asi_Stop;
    c->hcenter = cclks / 2;
    c->vtotal = ms->total_rows - 1;
    c->vbstrt = (UWORD)(s->vblank.asi_Start / cclks);
    c->vbstop = (UWORD)(s->vblank.asi_Stop / cclks);
    c->vsstrt = (UWORD)(s->vsync.asi_Start / cclks);
    c->vsstop = (UWORD)(s->vsync.asi_Stop / cclks);
    c->beamcon0 = ms->BeamCon0;
    return 0;
}

static void add_timing(const struct timing *t, struct MonitorSpec *def)
{
    struct MonitorSpec *ms;
    struct SpecialMonitor *sm;
    if (FindName(&GfxBase->MonitorList, (STRPTR)t->name))
        return;
    ms = (struct MonitorSpec *)GfxNew(MONITOR_SPEC_TYPE);
    sm = (struct SpecialMonitor *)GfxNew(SPECIAL_MONITOR_TYPE);
    if (!ms || !sm) {
        if (ms) GfxFree((struct ExtendedNode *)ms);
        if (sm) GfxFree((struct ExtendedNode *)sm);
        return;
    }
    ms->ms_Node.xln_Name = (char *)t->name;
    ms->ms_Flags = MSF_REQUEST_PAL | MSF_REQUEST_SPECIAL | MSF_DOUBLE_SPRITES;
    ms->ratioh = t->ratioh;
    ms->ratiov = t->ratiov;
    ms->total_rows = t->rows;
    ms->total_colorclocks = t->cclks;
    ms->DeniseMaxDisplayColumn = t->dmax;
    ms->DeniseMinDisplayColumn = t->dmin;
    ms->BeamCon0 = 0x1B88;
    ms->min_row = t->minrow;
    ms->ms_xoffset = 9;
    ms->ms_LegalView.MinX = t->left;
    ms->ms_LegalView.MinY = t->top;
    ms->ms_LegalView.MaxX = t->right;
    ms->ms_LegalView.MaxY = t->bottom;
    if (def) {
        ms->ms_transform = def->ms_transform;
        ms->ms_translate = def->ms_translate;
        ms->ms_scale = def->ms_scale;
        ms->ms_maxoscan = def->ms_maxoscan;
        ms->ms_videoscan = def->ms_videoscan;
    }
    ms->DisplayInfoDataBase.lh_Head = (struct Node *)&ms->DisplayInfoDataBase.lh_Tail;
    ms->DisplayInfoDataBase.lh_Tail = NULL;
    ms->DisplayInfoDataBase.lh_TailPred = (struct Node *)&ms->DisplayInfoDataBase.lh_Head;
    InitSemaphore(&ms->DisplayInfoDataBaseSemaphore);
    sm->do_monitor = do_monitor;
    sm->hblank.asi_Start = 0x0001;
    sm->hblank.asi_Stop = 0x0021;
    sm->vblank.asi_Start = 0;
    sm->vblank.asi_Stop = t->vb_stop;
    sm->hsync.asi_Start = 0x000B;
    sm->hsync.asi_Stop = 0x0015;
    sm->vsync.asi_Start = t->vs_start;
    sm->vsync.asi_Stop = t->vs_stop;
    ms->ms_Special = sm;
    ObtainSemaphore(GfxBase->MonitorListSemaphore);
    AddTail(&GfxBase->MonitorList, (struct Node *)ms);
    ReleaseSemaphore(GfxBase->MonitorListSemaphore);
}

/* ---- the patches --------------------------------------------------------- */

typedef ULONG (*data_fn)(REG(a0, APTR), REG(a1, UBYTE *), REG(d0, ULONG), REG(d1, ULONG), REG(d2, ULONG), REG(a6, struct Library *));
typedef ULONG (*next_fn)(REG(d0, ULONG), REG(a6, struct Library *));
static data_fn old_data;
static next_fn old_next;
static int filtered;                                /* the ECS view on an AGA machine */

static void name_it(char *name)
{
    static const char prefix[] = CHIP_NAME " ";
    int have = 0, skip = sizeof prefix - 1, i;
    while (have < NAME_ROOM && name[have]) have++;
    if (!have) return;
    if (have + skip >= NAME_ROOM) have = NAME_ROOM - 1 - skip;
    name[have + skip] = 0;
    for (i = have - 1; i >= 0; i--) name[i + skip] = name[i];
    for (i = 0; i < skip; i++) name[i] = prefix[i];
}

static UWORD ecs_depth(ULONG id)
{
    if (id & SUPER_KEY) return 2;
    if (id & HIRES_KEY) return 4;
    return 6;
}

static ULONG data_patch(REG(a0, APTR handle), REG(a1, UBYTE *buf), REG(d0, ULONG size), REG(d1, ULONG tag), REG(d2, ULONG id), REG(a6, struct Library *gfx))
{
    ULONG got = old_data(handle, buf, size, tag, id, gfx);
    if (got) {
        ULONG mode = ((struct QueryHeader *)buf)->DisplayID;
        if (NATIVE(mode)) {
            if (tag == DTAG_NAME && size >= sizeof(struct NameInfo) - 8)
                name_it((char *)((struct NameInfo *)buf)->Name);
            if (filtered && tag == DTAG_DIMS && size >= 18 && ((struct DimensionInfo *)buf)->MaxDepth > ecs_depth(mode))
                ((struct DimensionInfo *)buf)->MaxDepth = ecs_depth(mode);
        }
    }
    return got;
}

static ULONG next_patch(REG(d0, ULONG id), REG(a6, struct Library *gfx))
{
    do
        id = old_next(id, gfx);
    while (id != INVALID_ID && (MONITOR_OF(id) == DBLNTSC_MONITOR_ID || MONITOR_OF(id) == DBLPAL_MONITOR_ID));
    return id;
}

/* ---- */

static void run_drivers(void)
{
    if (!(DOSBase = (struct DosLibrary *)OpenLibrary((CONST_STRPTR)"dos.library", 37)))
        return;
    for (ULONG i = 0; i < sizeof drivers / sizeof drivers[0]; i++) {
        BPTR f;
        if (FindName(&GfxBase->MonitorList, (STRPTR)drivers[i].monitor))
            continue;
        if (!(f = Open((CONST_STRPTR)drivers[i].file, MODE_OLDFILE)))
            continue;
        Close(f);
        SystemTags((CONST_STRPTR)drivers[i].file, SYS_Input, 0, SYS_Output, 0, TAG_DONE);
    }
    CloseLibrary((struct Library *)DOSBase);
}

int start(void)
{
    static struct SignalSemaphore marker;
    struct Process *me;
    struct CommandLineInterface *cli;
    int own_gfx, have_chips;
    struct MonitorSpec *def;

    {
        struct ExecBase **p = (struct ExecBase **)4;
        __asm__ ("" : "+r"(p));                     /* GCC knows no object at 4 */
        SysBase = *p;
    }
    me = (struct Process *)FindTask(NULL);
    if (!me->pr_CLI) {                              /* from its icon: nothing to do */
        struct Message *m;
        WaitPort(&me->pr_MsgPort);
        m = GetMsg(&me->pr_MsgPort);
        Forbid();
        ReplyMsg(m);
        return 0;
    }
    if (!(GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39)))
        return RETURN_WARN;
    have_chips = (GfxBase->ChipRevBits0 & NEEDS) == NEEDS;
#ifndef CHIPSET_AGA
    filtered = (GfxBase->ChipRevBits0 & (GFXF_AA_ALICE | GFXF_AA_LISA)) != 0;
#endif
    Forbid();
    if (!have_chips || FindSemaphore((STRPTR)MARKER)
#ifndef CHIPSET_AGA
        || FindSemaphore((STRPTR)"AGA.monitor")
#endif
        ) {
        Permit();
        CloseLibrary((struct Library *)GfxBase);
        return RETURN_OK;
    }
    marker.ss_Link.ln_Name = (char *)MARKER;
    marker.ss_Link.ln_Pri = 0;
    AddSemaphore(&marker);
    Permit();

    own_gfx = GfxBase->LibNode.lib_Version == 47 && GfxBase->LibNode.lib_Revision >= 100;
    if (own_gfx) {
        def = OpenMonitor(NULL, 0);
        for (ULONG i = 0; i < TIMINGS; i++)
            add_timing(&timings[i], def);
        if (def) CloseMonitor(def);
    } else
        run_drivers();

    Forbid();
    old_data = (data_fn)SetFunction((struct Library *)GfxBase, -756, (APTR)data_patch);
    if (filtered)
        old_next = (next_fn)SetFunction((struct Library *)GfxBase, -732, (APTR)next_patch);
    CacheClearU();
    Permit();

    /* the patches are in this program: the Shell must not unload it */
    cli = BADDR(me->pr_CLI);
    cli->cli_Module = 0;
    return RETURN_OK;                               /* graphics.library stays open too */
}
