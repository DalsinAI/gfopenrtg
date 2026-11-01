/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * PixTest: what OpenRTG's pixel conversions and blits draw, as checksums,
 * and what they cost, on its own 640 x 480 screens of 8, 16 and 32 bits.
 * Run it with one openrtg.library and then another: the lines starting
 * "sum" must be the same (lib/screens.c's blits and pen lookups changed
 * how, not what); the lines starting "time" are the costs.
 *   - ReadPixelArray8 of the whole screen (pixels of any colour, so on 16
 *     and 32-bit each is a nearest-pen search), again, and after a palette
 *     change; ReadPixelLine8; ReadPixel;
 *   - 100 icon-like masked blits (BltMaskBitMapRastPort, minterm 0xE0) from
 *     a bitmap of the screen's format into a partly covered window, and from
 *     a planar bitmap; on 8-bit, through a plane mask too;
 *   - a 320 x 256 x 8 planar picture blitted to the screen (copy, two other
 *     minterms, a plane mask), and the screen blitted to planes;
 *   - the masked blits once with ORTG_DRAW_CPU_ONLY and once through
 *     OpenGPU, compared.
 *   PixTest [DEPTH=8|16|32]      (all three when not given)
 * Returns 0, or 10 when the OpenGPU and CPU masked blits differ. */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/displayinfo.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <proto/openrtg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct Library *OpenRTGBase;
struct Device *TimerBase;
static struct timerequest treq;
static int fails;

static double now_ms(void)
{
    struct EClockVal e;
    ULONG hz = ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + (double)e.ev_lo) * 1000.0 / (double)hz;
}

static ULONG fnv(ULONG h, const UBYTE *p, ULONG n)
{
    while (n--) { h ^= *p++; h *= 16777619UL; }
    return h;
}

static ULONG rs = 1;
static ULONG rnd(void) { rs = rs * 1103515245UL + 12345UL; return rs >> 8; }

/* The 640 x 480 OpenRTG ModeID of a depth (32 may be listed as 24). */
static ULONG find_mode(int depth)
{
    ULONG id = INVALID_ID;
    struct DimensionInfo di;
    while ((id = NextDisplayInfo(id)) != INVALID_ID) {
        if ((id >> 28) != 6 || !(id & 0x1000)) continue;
        if (!GetDisplayInfoData(NULL, (UBYTE *)&di, sizeof di, DTAG_DIMS, id)) continue;
        if (!(di.MaxDepth == depth || (depth == 32 && di.MaxDepth == 24))) continue;
        if (di.Nominal.MaxX - di.Nominal.MinX + 1 == 640 && di.Nominal.MaxY - di.Nominal.MinY + 1 == 480) return id;
    }
    return INVALID_ID;
}

/* A rectangle's pixels as they are (ORTG_ReadPixels RAW), checksummed. */
static UBYTE *grab_buf;
static ULONG grab(struct RastPort *rp, int x, int y, int w, int h, int bpp)
{
    struct OpenRTGPixels px;
    memset(grab_buf, 0, (ULONG)w * h * bpp);
    px.data = grab_buf; px.x = px.y = 0; px.modulo = w * bpp; px.format = ORTG_PIX_RAW; px.ctable = NULL;
    px.width = w; px.height = h; px.dest_width = px.dest_height = 0;
    ORTG_ReadPixels(rp, x, y, &px);
    return fnv(2166136261UL, grab_buf, (ULONG)w * h * bpp);
}

static ULONG planes_sum(struct BitMap *bm)
{
    ULONG h = 2166136261UL;
    for (int p = 0; p < bm->Depth; p++) h = fnv(h, bm->Planes[p], (ULONG)bm->BytesPerRow * bm->Rows);
    return h;
}

static void set_palette(struct Screen *s, ULONG salt)
{
    static ULONG t[1 + 3 * 256 + 1];
    t[0] = 256UL << 16;
    for (int i = 0; i < 256; i++) {
        ULONG c;
        switch (i % 5) {
        case 0: c = (ULONG)i * 0x010101UL; break;                         /* greys */
        case 1: c = (ULONG)(i * 2654435761UL ^ salt) & 0xFFFFFF; break;  /* anything */
        case 2: c = (ULONG)((i - 2) * 0x010101UL) ^ 0x000001; break;      /* near a grey */
        case 3: c = 0xFF0000UL >> (i % 3 * 8); break;                     /* the same few twice and more */
        default: c = (ULONG)(i * 40503UL) & 0xF8FCF8; break;              /* exact in 16-bit */
        }
        t[1 + 3 * i] = ((c >> 16) & 255) * 0x01010101UL;
        t[2 + 3 * i] = ((c >> 8) & 255) * 0x01010101UL;
        t[3 + 3 * i] = (c & 255) * 0x01010101UL;
    }
    t[1 + 3 * 256] = 0;
    LoadRGB32(&s->ViewPort, t);
}

/* The screen's pixels: of any colour on 16 and 32-bit, runs of one colour
 * and colours of the palette among them (as pictures and GUIs have). */
static void fill_screen(struct Screen *s, int bpp)
{
    static ULONG row[640];
    struct OpenRTGPixels px;
    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 640; x++) {
            ULONG v;
            if (bpp == 1) v = (ULONG)((x / 7) ^ (y * 3) ^ (x * y >> 6)) & 255;
            else if (y < 160) v = (ULONG)(x * 0x10203 + y * 0x30201) & 0xFFFFFF;    /* gradients */
            else if (y < 320) v = (ULONG)(((x / 16) * 2654435761UL) ^ (y / 8) * 40503UL) & 0xFFFFFF;  /* blocks */
            else v = rnd() & 0xFFFFFF;                                            /* noise */
            if (bpp == 2) v = ((v >> 8) & 0xF800) | ((v >> 5) & 0x07E0) | ((v >> 3) & 0x1F);
            if (bpp == 1) ((UBYTE *)row)[x] = (UBYTE)v;
            else if (bpp == 2) ((UWORD *)row)[x] = (UWORD)v;
            else row[x] = v;
        }
        px.data = row; px.x = px.y = 0; px.modulo = 640 * bpp; px.format = ORTG_PIX_RAW; px.ctable = NULL;
        px.width = 640; px.height = 1; px.dest_width = px.dest_height = 0;
        ORTG_WritePixels(&s->RastPort, 0, y, &px);
    }
}

static void fill_planar(struct BitMap *bm, ULONG salt)
{
    for (int p = 0; p < bm->Depth; p++)
        for (ULONG i = 0; i < (ULONG)bm->BytesPerRow * bm->Rows; i++)
            bm->Planes[p][i] = (UBYTE)(((i * (p + 3)) ^ (i >> (p + 2)) ^ salt) * 37);
}

static ULONG stat_before[ORTG_STAT_COUNT * 4];
static void stats_start(void) { ORTG_DrawStats(stat_before, ORTG_STAT_COUNT, 0); }
static void stats_say(int kind, const char *what)
{
    static ULONG a[ORTG_STAT_COUNT * 4];
    ORTG_DrawStats(a, ORTG_STAT_COUNT, 0);
    printf("info  %-26s pieces %lu OpenGPU, %lu CPU\n", what, (unsigned long)(a[kind * 4] - stat_before[kind * 4]),
           (unsigned long)(a[kind * 4 + 1] - stat_before[kind * 4 + 1]));
}

#define IW 40
#define IH 32

static void test_depth(int depth)
{
    int bpp = depth == 8 ? 1 : depth == 16 ? 2 : 4, i;
    ULONG id = find_mode(depth);
    struct Screen *s;
    struct Window *w, *cover;
    struct RastPort tmp, irp;
    struct BitMap *tbm, *icon, *picon, *pic, *pic5, *back, *back5;
    UBYTE *mask, *arr;
    double t0, t1;
    char tag[8];
    sprintf(tag, "D%d", depth);
    if (id == INVALID_ID) { printf("info  %s: no 640 x 480 OpenRTG mode of this depth; skipped\n", tag); return; }
    s = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, 640, SA_Height, 480, SA_Depth, depth,
                       SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN, TAG_DONE);
    if (!s) { printf("info  %s: OpenScreen failed; skipped\n", tag); fails++; return; }
    set_palette(s, 0);
    rs = 1;
    fill_screen(s, bpp);
    arr = AllocVec(640 * 480 + 64, MEMF_ANY | MEMF_CLEAR);
    grab_buf = AllocVec(640 * 480 * 4, MEMF_ANY);
    tbm = AllocBitMap(640, 1, 8, BMF_CLEAR, NULL);
    InitRastPort(&tmp); tmp.BitMap = tbm;
    if (!arr || !grab_buf || !tbm) { printf("info  %s: no memory\n", tag); fails++; goto out0; }
    printf("sum   %s screen as filled          %08lx\n", tag, (unsigned long)grab(&s->RastPort, 0, 0, 640, 480, bpp));

    /* ---- reading pens ---- */
    stats_start();
    t0 = now_ms();
    ReadPixelArray8(&s->RastPort, 0, 0, 639, 479, arr, &tmp);
    t1 = now_ms();
    printf("sum   %s ReadPixelArray8 640x480    %08lx\n", tag, (unsigned long)fnv(2166136261UL, arr, 640 * 480));
    printf("time  %s ReadPixelArray8 640x480    %9.1f ms\n", tag, t1 - t0);
    memset(arr, 0, 640 * 480);
    t0 = now_ms();
    ReadPixelArray8(&s->RastPort, 0, 0, 639, 479, arr, &tmp);
    t1 = now_ms();
    printf("sum   %s ReadPixelArray8 again      %08lx\n", tag, (unsigned long)fnv(2166136261UL, arr, 640 * 480));
    printf("time  %s ReadPixelArray8 again      %9.1f ms\n", tag, t1 - t0);
    SetRGB32(&s->ViewPort, 1, 0x12345678, 0x9ABCDEF0, 0x0F0F0F0F);
    SetRGB32(&s->ViewPort, 7, 0x40404040, 0x41414141, 0x42424242);
    set_palette(s, 0x5A5A5A);                       /* most pens change */
    memset(arr, 0, 640 * 480);
    ReadPixelArray8(&s->RastPort, 0, 0, 639, 479, arr, &tmp);
    printf("sum   %s ReadPixelArray8 new palette %08lx\n", tag, (unsigned long)fnv(2166136261UL, arr, 640 * 480));
    memset(arr, 0, 640 * 480);
    t0 = now_ms();
    for (i = 0; i < 480; i += 3) ReadPixelLine8(&s->RastPort, 0, i, 640, arr + i * 640, &tmp);
    t1 = now_ms();
    printf("sum   %s ReadPixelLine8 160 rows    %08lx\n", tag, (unsigned long)fnv(2166136261UL, arr, 640 * 480));
    printf("time  %s ReadPixelLine8 160 rows    %9.1f ms\n", tag, t1 - t0);
    {
        ULONG h = 2166136261UL;
        t0 = now_ms();
        for (i = 0; i < 4000; i++) { UBYTE v = (UBYTE)ReadPixel(&s->RastPort, (i * 7919) % 640, (i * 104729) % 480); h = fnv(h, &v, 1); }
        t1 = now_ms();
        printf("sum   %s ReadPixel x4000            %08lx\n", tag, (unsigned long)h);
        printf("time  %s ReadPixel x4000            %9.1f ms\n", tag, t1 - t0);
    }
    stats_say(ORTG_STAT_CHUNKY_READ, "ReadPixelArray8/Line8");
    set_palette(s, 0);

    /* ---- masked blits ---- */
    w = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, 100, WA_Top, 60, WA_Width, 420, WA_Height, 300,
                       WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, TAG_DONE);
    cover = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, 230, WA_Top, 140, WA_Width, 90, WA_Height, 70,
                           WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, TAG_DONE);
    icon = AllocBitMap(IW, IH, depth, BMF_CLEAR, s->RastPort.BitMap);
    picon = AllocBitMap(IW, IH, 4, BMF_CLEAR, NULL);
    mask = AllocVec(((IW + 15) / 16 * 2) * IH, MEMF_CHIP | MEMF_CLEAR);
    if (!w || !cover || !icon || !picon || !mask) { printf("info  %s: no windows or bitmaps\n", tag); fails++; goto out1; }
    {
        static ULONG ipx[IW * IH];
        struct OpenRTGPixels px;
        int mod = (IW + 15) / 16 * 2;
        for (i = 0; i < IW * IH; i++) {
            ULONG v = (ULONG)(i * 2654435761UL) & 0xFFFFFF;
            if (bpp == 1) ((UBYTE *)ipx)[i] = (UBYTE)v;
            else if (bpp == 2) ((UWORD *)ipx)[i] = (UWORD)(((v >> 8) & 0xF800) | ((v >> 5) & 0x07E0) | ((v >> 3) & 0x1F));
            else ipx[i] = v;
        }
        InitRastPort(&irp); irp.BitMap = icon;
        px.data = ipx; px.x = px.y = 0; px.modulo = IW * bpp; px.format = ORTG_PIX_RAW; px.ctable = NULL;
        px.width = IW; px.height = IH; px.dest_width = px.dest_height = 0;
        ORTG_WritePixels(&irp, 0, 0, &px);
        fill_planar(picon, 3);
        /* an icon's mask: a disc with a solid body, a hole and a ragged edge */
        for (int y = 0; y < IH; y++)
            for (int x = 0; x < IW; x++) {
                int dx = x - 20, dy = y - 16, on = dx * dx + dy * dy < 15 * 15 || (y >= 10 && y < 24 && x >= 4 && x < 36);
                if (dx * dx + dy * dy < 9) on = 0;
                if (y < 3 && (x & 1)) on = 1;
                if (on) mask[y * mod + x / 8] |= 0x80 >> (x & 7);
            }
    }
    SetAPen(w->RPort, 0); RectFill(w->RPort, 0, 0, 419, 299);
    stats_start();
    t0 = now_ms();
    for (i = 0; i < 100; i++)
        BltMaskBitMapRastPort(icon, 0, 0, w->RPort, (i % 10) * 41 + (i / 10), (i / 10) * 29, IW, IH, 0xE0, mask);
    (void)ReadPixel(w->RPort, 0, 0);               /* what OpenGPU drew is done */
    t1 = now_ms();
    stats_say(ORTG_STAT_BLIT_MASKED, "100 masked icons");
    printf("time  %s 100 masked icon blits      %9.1f ms\n", tag, t1 - t0);
    for (i = 0; i < 20; i++)                        /* parts of it, from odd bits */
        BltMaskBitMapRastPort(icon, 3 + i % 5, 2, w->RPort, 13 + i * 19, 250 - (i & 3), 30, 25, 0xE0, mask);
    t0 = now_ms();
    for (i = 0; i < 20; i++)                        /* a planar icon */
        BltMaskBitMapRastPort(picon, i % 3, 0, w->RPort, i * 20, 200 + (i & 7), IW - 3, IH, 0xE0, mask);
    (void)ReadPixel(w->RPort, 0, 0);
    t1 = now_ms();
    printf("time  %s 20 masked planar icons     %9.1f ms\n", tag, t1 - t0);
    if (bpp == 1) {
        w->RPort->Mask = 0x3F;
        for (i = 0; i < 10; i++) BltMaskBitMapRastPort(icon, 0, 0, w->RPort, 300 + i * 7, 100 + i * 9, IW, IH, 0xE0, mask);
        w->RPort->Mask = 0xFF;
    }
    printf("sum   %s masked blits, window       %08lx\n", tag, (unsigned long)grab(w->RPort, 0, 0, 420, 300, bpp));
    printf("sum   %s masked blits, screen       %08lx\n", tag, (unsigned long)grab(&s->RastPort, 0, 0, 640, 480, bpp));

    /* the same through OpenGPU and with the CPU only, on the screen (no layers) */
    {
        ULONG a, b;
        SetAPen(&s->RastPort, 2); RectFill(&s->RastPort, 0, 0, 639, 99);
        ORTG_DrawStats(NULL, 0, ORTG_DRAW_CPU_ONLY);
        for (i = 0; i < 20; i++) BltMaskBitMapRastPort(icon, i % 4, 0, &s->RastPort, i * 15, (i & 3) * 9, IW - 4, IH, 0xE0, mask);
        ORTG_DrawStats(NULL, 0, ORTG_DRAW_OPENGPU);
        for (i = 0; i < 20; i++) BltMaskBitMapRastPort(icon, i % 4, 0, &s->RastPort, 320 + i * 15, (i & 3) * 9, IW - 4, IH, 0xE0, mask);
        a = grab(&s->RastPort, 0, 0, 320, 100, bpp);
        b = grab(&s->RastPort, 320, 0, 320, 100, bpp);
        printf("sum   %s masked CPU-only half       %08lx\n", tag, (unsigned long)a);
        printf("check %s masked OpenGPU = CPU-only: %s\n", tag, a == b ? "the same" : "DIFFERENT");
        if (a != b) fails++;
    }

    /* ---- planar to chunky and back ---- */
    pic = AllocBitMap(320, 256, 8, BMF_CLEAR, NULL);
    pic5 = AllocBitMap(320, 256, 5, BMF_CLEAR, NULL);
    back = AllocBitMap(320, 256, 8, BMF_CLEAR, NULL);
    back5 = AllocBitMap(320, 256, 5, BMF_CLEAR, NULL);
    if (pic && pic5 && back && back5) {
        fill_planar(pic, 0); fill_planar(pic5, 77); fill_planar(back5, 9);
        stats_start();
        t0 = now_ms();
        BltBitMapRastPort(pic, 0, 0, &s->RastPort, 7, 105, 320, 256, 0xC0);
        (void)ReadPixel(&s->RastPort, 0, 0);
        t1 = now_ms();
        stats_say(ORTG_STAT_BLIT_PLANAR, "planar 320x256x8 -> RTG");
        printf("time  %s planar 320x256x8 -> RTG    %9.1f ms\n", tag, t1 - t0);
        BltBitMapRastPort(pic5, 3, 1, &s->RastPort, 330, 105, 300, 200, 0x60);
        BltBitMap(pic, 11, 0, s->RastPort.BitMap, 330, 300, 290, 170, 0x30, 0x0F, NULL);
        printf("sum   %s planar -> RTG              %08lx\n", tag, (unsigned long)grab(&s->RastPort, 0, 100, 640, 380, bpp));
        t0 = now_ms();
        BltBitMap(s->RastPort.BitMap, 7, 105, back, 0, 0, 320, 256, 0xC0, 0xFF, NULL);
        t1 = now_ms();
        printf("time  %s RTG -> planar 320x256x8    %9.1f ms\n", tag, t1 - t0);
        BltBitMap(s->RastPort.BitMap, 101, 33, back5, 5, 3, 300, 240, 0x60, 0x1B, NULL);
        printf("sum   %s RTG -> planar 8            %08lx\n", tag, (unsigned long)planes_sum(back));
        printf("sum   %s RTG -> planar 5, 0x60 1B   %08lx\n", tag, (unsigned long)planes_sum(back5));
    } else { printf("info  %s: no planar bitmaps\n", tag); fails++; }
    if (pic) FreeBitMap(pic);
    if (pic5) FreeBitMap(pic5);
    if (back) FreeBitMap(back);
    if (back5) FreeBitMap(back5);
out1:
    if (cover) CloseWindow(cover);
    if (w) CloseWindow(w);
    if (icon) FreeBitMap(icon);
    if (picon) FreeBitMap(picon);
    if (mask) FreeVec(mask);
out0:
    if (tbm) FreeBitMap(tbm);
    if (arr) FreeVec(arr);
    if (grab_buf) FreeVec(grab_buf);
    CloseScreen(s);
}

int main(int argc, char **argv)
{
    int only = 0;
    if (argc > 1 && !strncmp(argv[1], "DEPTH=", 6)) only = atoi(argv[1] + 6);
    if (!(OpenRTGBase = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, 0)) || (OpenRTGBase->lib_Version == 0 && OpenRTGBase->lib_Revision < 11)) {
        printf("PixTest: needs openrtg.library 0.11\n");
        return 20;
    }
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&treq, 0)) { printf("PixTest: no timer.device\n"); return 20; }
    TimerBase = treq.tr_node.io_Device;
    printf("info  openrtg.library %d.%d\n", OpenRTGBase->lib_Version, OpenRTGBase->lib_Revision);
    if (!only || only == 8) test_depth(8);
    if (!only || only == 16) test_depth(16);
    if (!only || only == 32) test_depth(32);
    CloseDevice((struct IORequest *)&treq);
    CloseLibrary(OpenRTGBase);
    printf("PixTest: %s\n", fails ? "FAILED" : "done");
    return fails ? 10 : 0;
}
