/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * PresentCheck: M8's level 3 check for frames in memory (ORTG_SURF_MEMORY).
 * Each case presents one frame with ORTG_Present, reads back what is on the
 * screen, then writes the same frame the way before M8 (ORTG_WritePixels)
 * and reads that back: the two must be the same, pixel for pixel. Cases:
 *   - screens of 32, 16 and 8 bits (BestModeID; GFOpenRTG's modes where its
 *     screens are on);
 *   - into the screen's own RastPort, a window, a window half covered by
 *     another, and a bitmap of the screen's kind (a RastPort of its own);
 *   - frames in ARGB, 0RGB and RGB (3 bytes), whole and a damaged part
 *     (src) only;
 * and the "present memory" count goes up by each present's pixels.
 * The other kinds have their own lab programs: VIRGL (MGLTest, W3DCheck,
 * GL.module's demos), RETAINED (RetainedTest), MEDIA (ACMWindow).
 *   PresentCheck DEPTH/N (only that depth)
 * Prints one line a failing case and a last line "PresentCheck: N checks, M failed".
 * Returns 0, 5 without gfopenrtg.library 0.30, 10 when any case fails. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/displayinfo.h>
#include <graphics/modeid.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gfopenrtg.h>
#include <stdio.h>
#include <string.h>

struct Library *OpenRTGBase;
struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;

#define FW 96
#define FH 64
static int checks, failed;

static void check(int ok, const char *what, int depth, const char *target, const char *fmt)
{
    checks++;
    if (!ok) { failed++; printf("FAIL depth %d, %s, %s: %s\n", depth, target, fmt, what); }
}

/* the frame: bands of colour and a gradient, so a wrong byte order or a shifted row shows */
static void make_frame(UBYTE *f, ULONG format, ULONG *stride)
{
    int x, y, bpp = format == ORTG_PIX_RGB ? 3 : 4;
    *stride = FW * bpp + 8;                                 /* a modulo that isn't the width */
    for (y = 0; y < FH; y++)
        for (x = 0; x < FW; x++) {
            UBYTE r = (UBYTE)(x * 255 / (FW - 1)), g = (UBYTE)(y * 255 / (FH - 1)), b = (UBYTE)((x / 12) & 1 ? 0xc0 : 0x30);
            UBYTE *p = f + y * *stride + x * bpp;
            if (format == ORTG_PIX_RGB) { p[0] = r; p[1] = g; p[2] = b; }
            else { p[0] = format == ORTG_PIX_ARGB ? 0xff : 0x00; p[1] = r; p[2] = g; p[3] = b; }
        }
}

/* what is on the screen where the frame went (its visible pixels, as a viewer sees them) */
static void grab(struct RastPort *rp, LONG x, LONG y, ULONG *out)
{
    struct OpenRTGPixels px;
    memset(out, 0x5a, FW * FH * 4);
    px.data = out; px.x = 0; px.y = 0; px.modulo = FW * 4; px.format = ORTG_PIX_ARGB; px.ctable = NULL;
    px.width = FW; px.height = FH; px.dest_width = px.dest_height = 0;
    ORTG_ReadPixels(rp, x, y, &px);
}

static ULONG stat_memory(void)
{
    ULONG c[ORTG_STAT_COUNT * 4];
    memset(c, 0, sizeof c);
    ORTG_DrawStats(c, ORTG_STAT_COUNT, 0);
    return c[ORTG_STAT_PRESENT_MEMORY * 4 + 3];      /* the CPU's pixels */
}

static void one(struct RastPort *rp, LONG x, LONG y, struct RastPort *view, LONG vx, LONG vy,
                int depth, const char *target, ULONG *a, ULONG *b)
{
    static const ULONG formats[3] = { ORTG_PIX_ARGB, ORTG_PIX_0RGB, ORTG_PIX_RGB };
    static const char *const names[3] = { "ARGB", "0RGB", "RGB" };
    UBYTE *frame = AllocVec((FW * 4 + 8) * FH, MEMF_ANY | MEMF_CLEAR);
    int i, part;
    if (!frame) { check(0, "no memory", depth, target, "-"); return; }
    for (i = 0; i < 3; i++)
        for (part = 0; part < 2; part++) {
            struct ORTG_PresentInfo pi;
            struct Rectangle damage = { 10, 7, 61, 40 };
            struct OpenRTGPixels px;
            ULONG stride, s0, s1;
            LONG r;
            char fmt[16];
            sprintf(fmt, "%s%s", names[i], part ? " part" : "");
            make_frame(frame, formats[i], &stride);
            SetRast(rp, 1);
            memset(&pi, 0, sizeof pi);
            pi.size = sizeof pi; pi.kind = ORTG_SURF_MEMORY; pi.memory = frame; pi.stride = stride; pi.format = formats[i];
            pi.width = FW; pi.height = FH; pi.rp = rp; pi.x = x; pi.y = y; pi.src = part ? &damage : NULL;
            s0 = stat_memory();
            r = ORTG_Present(&pi);
            s1 = stat_memory();
            check(r >= 0, "ORTG_Present failed", depth, target, fmt);
            check(s1 - s0 == (part ? 52UL * 34UL : (ULONG)FW * FH), "present memory count", depth, target, fmt);
            grab(view, vx, vy, a);
            /* before M8: the same pixels by ORTG_WritePixels */
            SetRast(rp, 1);
            px.data = frame; px.modulo = (long)stride; px.format = formats[i]; px.ctable = NULL; px.dest_width = px.dest_height = 0;
            if (part) { px.x = 10; px.y = 7; px.width = 52; px.height = 34; ORTG_WritePixels(rp, x + 10, y + 7, &px); }
            else { px.x = 0; px.y = 0; px.width = FW; px.height = FH; ORTG_WritePixels(rp, x, y, &px); }
            grab(view, vx, vy, b);
            check(!memcmp(a, b, FW * FH * 4), "pixels differ from ORTG_WritePixels'", depth, target, fmt);
        }
    FreeVec(frame);
}

static void run_depth(int depth, ULONG *a, ULONG *b)
{
    ULONG id = BestModeID(BIDTAG_NominalWidth, 640, BIDTAG_NominalHeight, 480, BIDTAG_Depth, depth, TAG_DONE);
    struct Screen *s;
    struct Window *w = NULL, *cover = NULL;
    struct BitMap *bm = NULL;
    struct RastPort brp;
    if (id == INVALID_ID) { printf("depth %d: no mode, skipped\n", depth); return; }
    s = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, 640, SA_Height, 480, SA_Depth, depth, SA_Quiet, TRUE,
                       SA_ShowTitle, FALSE, SA_Title, (ULONG)"PresentCheck", TAG_DONE);
    if (!s) { check(0, "can't open the screen", depth, "screen", "-"); return; }
    printf("depth %d: mode $%08lx, the screen's bitmap %ld bits\n", depth, (unsigned long)id,
           (long)GetBitMapAttr(s->RastPort.BitMap, BMA_DEPTH));
    one(&s->RastPort, 20, 20, &s->RastPort, 20, 20, depth, "screen", a, b);
    w = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, 200, WA_Top, 40, WA_InnerWidth, FW + 20, WA_InnerHeight, FH + 20,
                       WA_Borderless, FALSE, WA_SimpleRefresh, TRUE, WA_Activate, FALSE, TAG_DONE);
    if (w) {
        one(w->RPort, w->BorderLeft + 4, w->BorderTop + 4, &s->RastPort, w->LeftEdge + w->BorderLeft + 4,
            w->TopEdge + w->BorderTop + 4, depth, "window", a, b);
        /* a second window over the frame's right half: what shows must be the same both ways */
        cover = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, w->LeftEdge + w->BorderLeft + 4 + FW / 2,
                               WA_Top, w->TopEdge + 20, WA_Width, FW, WA_Height, FH, WA_Activate, FALSE, TAG_DONE);
        if (cover) {
            one(w->RPort, w->BorderLeft + 4, w->BorderTop + 4, &s->RastPort, w->LeftEdge + w->BorderLeft + 4,
                w->TopEdge + w->BorderTop + 4, depth, "covered window", a, b);
            CloseWindow(cover);
        } else check(0, "can't open the covering window", depth, "covered window", "-");
        CloseWindow(w);
    } else check(0, "can't open the window", depth, "window", "-");
    if ((bm = AllocBitMap(FW + 40, FH + 40, depth, BMF_CLEAR, s->RastPort.BitMap))) {
        InitRastPort(&brp); brp.BitMap = bm;
        one(&brp, 8, 8, &brp, 8, 8, depth, "bitmap", a, b);
        WaitBlit(); FreeBitMap(bm);
    } else check(0, "can't make the bitmap", depth, "bitmap", "-");
    CloseScreen(s);
}

int main(void)
{
    LONG args[1] = { 0 };
    struct RDArgs *rda = ReadArgs((CONST_STRPTR)"DEPTH/N", args, NULL);
    ULONG *a = NULL, *b = NULL;
    int rc = 10;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    OpenRTGBase = OpenLibrary((CONST_STRPTR)"gfopenrtg.library", 0);
    if (!IntuitionBase || !GfxBase || !OpenRTGBase) { printf("PresentCheck: no intuition/graphics 39 or gfopenrtg.library\n"); goto out; }
    printf("gfopenrtg.library %u.%u\n", OpenRTGBase->lib_Version, OpenRTGBase->lib_Revision);
    if (OpenRTGBase->lib_Version == 0 && OpenRTGBase->lib_Revision < 30) { printf("PresentCheck: no ORTG_Present (0.30)\n"); rc = 5; goto out; }
    a = AllocVec(FW * FH * 4, MEMF_ANY); b = AllocVec(FW * FH * 4, MEMF_ANY);
    if (!a || !b) goto out;
    if (args[0]) run_depth((int)*(LONG *)args[0], a, b);
    else { run_depth(32, a, b); run_depth(16, a, b); run_depth(8, a, b); }
    printf("PresentCheck: %d checks, %d failed\n", checks, failed);
    rc = failed ? 10 : 0;
out:
    if (a) FreeVec(a);
    if (b) FreeVec(b);
    if (OpenRTGBase) CloseLibrary(OpenRTGBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (rda) FreeArgs(rda);
    return rc;
}
