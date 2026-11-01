/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OpenRTGLineCheck: do Draw and PolyDraw on an GFOpenRTG screen draw what
 * graphics.library draws on a planar bitmap, and how fast?
 *
 * On an 8-bit GFOpenRTG screen of its own it draws the same random calls
 * (Move, Draw, PolyDraw, every DrawMode, LinePtrn through SetDrPt or set
 * straight, linpatcnt, the write mask) three ways: into a planar bitmap
 * (graphics.library, or GFOpenGfx's core, which matches it byte for byte),
 * into a smart refresh window partly covered by another (GFOpenRTG, in clip
 * pieces and the backing store) and into an GFOpenRTG bitmap with no layer;
 * it compares every pixel (ReadPixel), and cp, linpatcnt and Flags after
 * every call. Once through GFOpenGPU where GFOpenRTG uses it, once with
 * ORTG_DRAW_CPU_ONLY.
 * TIME draws 1000 slanted lines (solid JAM1, COMPLEMENT, a pattern) and
 * 1000 PolyDraw boxes in the window, and prints the milliseconds.
 * SHOW draws a COMPLEMENT rubber-band box (Move and four Draws, as programs
 * draw them, and a PolyDraw box) and a patterned PolyDraw, beside the same
 * drawn on a planar bitmap, as they are and four times as big, and holds the screen
 * for SECS seconds (default 60) for a screenshot.
 *   OpenRTGLineCheck [CHECK] [TIME] [SHOW] [SECS=n] [ROUNDS=n]   (none: CHECK and TIME)
 * Returns 0 when everything matches, 10 when something doesn't, 20 when it
 * can't run. gfopenrtg.library 0.11 or later. */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <proto/gfopenrtg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct Library *OpenRTGBase;
struct Device *TimerBase;
static struct timerequest treq;

#define W 160
#define H 100
#define WAYS 3
enum { REF, WIN, BMP };
static const char *const way_name[WAYS] = { "planar", "window", "bitmap" };
static struct RastPort *rps[WAYS];
static int fails, checks;

static ULONG rng = 2463534242UL;
static ULONG rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static LONG rnd_in(LONG lo, LONG hi) { return lo + (LONG)(rnd() % (ULONG)(hi - lo + 1)); }

/* E clock ticks (the low word: a run is far shorter than its wrap), and
 * ticks a millisecond */
static ULONG eclock_khz;
static ULONG now_ticks(void)
{
    struct EClockVal e;
    eclock_khz = ReadEClock(&e) / 1000;
    return e.ev_lo;
}

static void print_ms(const char *what, ULONG t0)
{
    ULONG t = now_ticks() - t0;
    printf("TIME %-44s %6lu.%lu ms\n", what, (unsigned long)(t / eclock_khz), (unsigned long)(t % eclock_khz * 10 / eclock_khz));
}

/* an end point inside, often straight or at 45 degrees from (px, py) */
static void end_point(LONG px, LONG py, WORD *xy)
{
    LONG x = rnd_in(0, W - 1), y = rnd_in(0, H - 1), s = (LONG)(rnd() % 4), d;
    if (s == 0) x = px;
    else if (s == 1) y = py;
    else if (s == 2) {
        d = x - px;
        y = py + (rnd() & 1 ? d : -d);
        if (y < 0 || y >= H) y = py;
    }
    xy[0] = (WORD)x; xy[1] = (WORD)y;
}

static int same_state(int c)
{
    int w, ok = 1;
    for (w = 1; w < WAYS; w++)
        if (rps[w]->cp_x != rps[REF]->cp_x || rps[w]->cp_y != rps[REF]->cp_y || rps[w]->linpatcnt != rps[REF]->linpatcnt
            || (rps[w]->Flags & FRST_DOT) != (rps[REF]->Flags & FRST_DOT)) {
            printf("  call %d: %s cp %d,%d linpatcnt %d FRST_DOT %d; planar %d,%d %d %d\n", c, way_name[w],
                   rps[w]->cp_x, rps[w]->cp_y, rps[w]->linpatcnt, rps[w]->Flags & FRST_DOT,
                   rps[REF]->cp_x, rps[REF]->cp_y, rps[REF]->linpatcnt, rps[REF]->Flags & FRST_DOT);
            ok = 0;
        }
    return ok;
}

static int same_pixels(const char *what)
{
    int w, ok = 1;
    for (w = 1; w < WAYS; w++) {
        LONG x, y, diff = 0, fx = 0, fy = 0;
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                if (ReadPixel(rps[w], x, y) != ReadPixel(rps[REF], x, y)) { if (!diff) { fx = x; fy = y; } diff++; }
        if (diff) {
            printf("  %s: %s %ld pixels differ, first at %ld,%ld (%ld, planar %ld)\n", what, way_name[w], (long)diff, (long)fx, (long)fy,
                   (long)ReadPixel(rps[w], fx, fy), (long)ReadPixel(rps[REF], fx, fy));
            ok = 0;
        }
    }
    return ok;
}

/* Each round: the same background, then twelve calls, the same on each way. */
static void check(int rounds, const char *how)
{
    static const UWORD pats[] = { 0xFFFF, 0xFFFF, 0x0000, 0xF0F0, 0xAAAA, 0xCCCC, 0x8001, 0xFF00 };
    int r, c, w, bad = 0;
    for (r = 0; r < rounds && bad < 5; r++) {
        ULONG seed = rnd(), cseed;
        int ok = 1;
        for (w = 0; w < WAYS; w++) {
            struct RastPort *rp = rps[w];
            int i;
            rng = seed;
            SetDrMd(rp, JAM1); SetWriteMask(rp, 0xFF); SetDrPt(rp, 0xFFFF);
            SetAPen(rp, rnd() & 255); RectFill(rp, 0, 0, W - 1, H - 1);
            for (i = 0; i < 4; i++) {
                LONG x0 = rnd_in(0, W - 1), y0 = rnd_in(0, H - 1), x1 = rnd_in(x0, W - 1), y1 = rnd_in(y0, H - 1);
                SetAPen(rp, rnd() & 255); RectFill(rp, x0, y0, x1, y1);
            }
            SetAPen(rp, rnd() & 255); SetBPen(rp, rnd() & 255);
            SetDrMd(rp, rnd() & 7);
            SetWriteMask(rp, rnd() & 3 ? 0xFF : rnd() & 255);
            SetDrPt(rp, rnd() & 1 ? pats[rnd() % 8] : (UWORD)rnd());
            Move(rp, rnd_in(0, W - 1), rnd_in(0, H - 1));
        }
        cseed = rnd();
        for (c = 0; c < 12 && ok; c++) {
            ULONG s = cseed + (ULONG)c * 7919UL;
            for (w = 0; w < WAYS; w++) {
                struct RastPort *rp = rps[w];
                int what, n, i;
                WORD xy[16];
                rng = s;
                what = (int)(rnd() % 10);
                n = 1 + (int)(rnd() % 8);
                switch (what) {
                case 0: Move(rp, rnd_in(0, W - 1), rnd_in(0, H - 1)); break;
                case 1: SetDrMd(rp, rnd() & 7); break;
                case 2: { UWORD p = rnd() & 1 ? pats[rnd() % 8] : (UWORD)rnd(); SetDrPt(rp, p); } break;
                case 3: { UWORD p = (UWORD)rnd(); rp->LinePtrn = p; rp->linpatcnt = (BYTE)rnd(); } break;
                case 4: case 5: case 6:
                    end_point(rp->cp_x, rp->cp_y, xy);
                    Draw(rp, xy[0], xy[1]);
                    break;
                default:
                    for (i = 0; i < n; i++) end_point(i ? xy[2 * i - 2] : rp->cp_x, i ? xy[2 * i - 1] : rp->cp_y, xy + 2 * i);
                    PolyDraw(rp, n, xy);
                }
            }
            checks++;
            if (!same_state(c)) ok = 0;
        }
        checks++;
        if (!same_pixels("pixels")) ok = 0;
        if (!ok) {
            printf("  %s round %d: DrawMode %d, LinePtrn %04x, Mask %02x\n", how, r, rps[REF]->DrawMode, rps[REF]->LinePtrn, rps[REF]->Mask);
            bad++; fails++;
        }
    }
    printf("%-10s %d rounds of 12 calls: %s\n", how, r, bad ? "DIFFERENT" : "the same as planar");
}

/* 1000 slanted lines, 1000 boxes */
static void timing(struct RastPort *rp, LONG w, LONG h)
{
    static const struct { const char *name; UBYTE mode; UWORD ptrn; } kinds[] = {
        { "JAM1 solid", JAM1, 0xFFFF }, { "COMPLEMENT", COMPLEMENT, 0xFFFF }, { "JAM2 F0F0", JAM2, 0xF0F0 },
    };
    int k, i;
    for (k = 0; k < 3; k++) {
        ULONG t0;
        char what[48];
        SetAPen(rp, 2); SetBPen(rp, 3); SetDrMd(rp, kinds[k].mode); SetDrPt(rp, kinds[k].ptrn); SetWriteMask(rp, 0xFF);
        t0 = now_ticks();
        for (i = 0; i < 1000; i++) {
            LONG x0 = (i * 37) % (w / 2), y0 = (i * 13) % (h / 3);
            Move(rp, x0, y0);
            Draw(rp, x0 + w / 2 - 1 - (i % 50), y0 + h / 2 + (i % 40));     /* slanted, about 280 by 170 */
        }
        sprintf(what, "1000 slanted lines, %s:", kinds[k].name);
        print_ms(what, t0);
    }
    {
        ULONG t0;
        SetDrMd(rp, COMPLEMENT); SetDrPt(rp, 0xFFFF);
        t0 = now_ticks();
        for (i = 0; i < 1000; i++) {
            WORD xy[8];
            LONG x0 = 10 + i % 100, y0 = 10 + i % 60;
            xy[0] = x0 + 200; xy[1] = y0; xy[2] = x0 + 200; xy[3] = y0 + 150; xy[4] = x0; xy[5] = y0 + 150; xy[6] = x0; xy[7] = y0;
            Move(rp, x0, y0);
            PolyDraw(rp, 4, xy);
        }
        print_ms("1000 COMPLEMENT PolyDraw boxes 200x150:", t0);
    }
}

/* SHOW: what is drawn into rp at (0, 0) and its planar twin, magnified */
static void scene(struct RastPort *rp)
{
    static WORD box[8] = { 40, 6, 40, 30, 6, 30, 6, 6 };
    static WORD zig[10] = { 20, 52, 32, 40, 44, 52, 56, 40, 68, 52 };
    int i;
    SetDrMd(rp, JAM1); SetDrPt(rp, 0xFFFF); SetWriteMask(rp, 0xFF);
    SetAPen(rp, 1); RectFill(rp, 0, 0, 79, 59);
    SetAPen(rp, 3); RectFill(rp, 0, 0, 22, 59);
    /* a rubber-band box as programs draw one: Move and four Draws */
    SetAPen(rp, 2); SetDrMd(rp, COMPLEMENT);
    Move(rp, 6, 6);
    for (i = 0; i < 4; i++) Draw(rp, box[2 * i], box[2 * i + 1]);
    /* and as a PolyDraw, a slanted one inside */
    Move(rp, 48, 6);
    {
        WORD b2[8] = { 74, 6, 74, 30, 48, 30, 48, 6 };
        PolyDraw(rp, 4, b2);
    }
    Move(rp, 50, 8); Draw(rp, 72, 18); Draw(rp, 52, 28);
    /* a patterned line on in JAM2: the pattern goes on from line to line */
    SetDrMd(rp, JAM2); SetAPen(rp, 2); SetBPen(rp, 0); SetDrPt(rp, 0xF0F0);
    Move(rp, 8, 40);
    PolyDraw(rp, 5, zig);
}

static void label(struct RastPort *rp, LONG x, LONG y, const char *t)
{
    Move(rp, x, y);
    Text(rp, (CONST_STRPTR)t, (ULONG)strlen(t));
}

/* rp's 80 by 60 at (0, 0), four times as big at (x, y) on the screen */
static void magnify(struct RastPort *z, struct RastPort *rp, LONG x0, LONG y0)
{
    LONG x, y;
    for (y = 0; y < 60; y++)
        for (x = 0; x < 80; x++) {
            SetAPen(z, ReadPixel(rp, x, y));
            RectFill(z, x0 + x * 4, y0 + y * 4, x0 + x * 4 + 2, y0 + y * 4 + 2);
        }
}

static void show(struct RastPort *srp, struct RastPort *ref, int secs)
{
    struct RastPort z = *srp, r = *srp;
    struct BitMap *bm = AllocBitMap(80, 60, 8, BMF_CLEAR, srp->BitMap);
    if (!bm) return;
    z.Layer = NULL; r.Layer = NULL; r.BitMap = bm;
    SetDrMd(&z, JAM1); SetDrPt(&z, 0xFFFF); SetWriteMask(&z, 0xFF);
    SetRast(srp, 0);
    SetAPen(&z, 1); SetBPen(&z, 0); SetDrMd(&z, JAM2);
    label(&z, 10, 20, "GFOpenRTG");
    label(&z, 400, 20, "planar (graphics.library)");
    scene(&r);
    scene(ref);
    BltBitMapRastPort(bm, 0, 0, &z, 10, 30, 80, 60, 0xC0);
    BltBitMapRastPort(ref->BitMap, 0, 0, &z, 400, 30, 80, 60, 0xC0);
    SetDrMd(&z, JAM1);
    magnify(&z, &r, 10, 100);
    magnify(&z, ref, 400, 100);
    SetAPen(&z, 1); SetDrMd(&z, JAM2);
    label(&z, 10, 370, "x4: COMPLEMENT box (Move, 4 Draws); PolyDraw box; 2 slanted Draws;");
    label(&z, 10, 382, "JAM2 PolyDraw, LinePtrn F0F0 (BgPen 0 between)");
    FreeBitMap(bm);
    Delay(50 * secs);
}

static int is(const char *a, const char *b)
{
    while (*b) {
        char c = *a++;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != *b++) return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    struct Screen *s;
    struct Window *win, *cover;
    struct BitMap *ref_bm, *rtg_bm;
    struct RastPort ref_rp, bmp_rp;
    struct OpenRTGBitMapInfo bi;
    ULONG id;
    int do_check = 0, do_time = 0, do_show = 0, secs = 60, rounds = 150, i;

    for (i = 1; i < argc; i++) {
        if (is(argv[i], "CHECK") && !argv[i][5]) do_check = 1;
        else if (is(argv[i], "TIME") && !argv[i][4]) do_time = 1;
        else if (is(argv[i], "SHOW") && !argv[i][4]) do_show = 1;
        else if (is(argv[i], "SECS=")) secs = atoi(argv[i] + 5);
        else if (is(argv[i], "ROUNDS=")) rounds = atoi(argv[i] + 7);
    }
    if (!do_check && !do_time && !do_show) do_check = do_time = 1;
    if (!(OpenRTGBase = OpenLibrary((CONST_STRPTR)OPENRTG_NAME, 0)) || (OpenRTGBase->lib_Version == 0 && OpenRTGBase->lib_Revision < 11)) {
        printf("OpenRTGLineCheck: needs gfopenrtg.library 0.11\n");
        return 20;
    }
    printf("OpenRTGLineCheck: gfopenrtg.library %d.%d\n", OpenRTGBase->lib_Version, OpenRTGBase->lib_Revision);
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, &treq.tr_node, 0)) { printf("OpenRTGLineCheck: no timer.device\n"); return 20; }
    TimerBase = treq.tr_node.io_Device;
    id = ORTG_BestMode(1, 800, 600, 8);
    s = OpenScreenTags(NULL, SA_DisplayID, id, SA_Depth, 8, SA_Width, 800, SA_Height, 600, SA_Title, (ULONG)"OpenRTGLineCheck", SA_ShowTitle, FALSE,
                       SA_Pens, (ULONG)"\xff\xff", SA_Quiet, TRUE, TAG_DONE);
    if (!s || !ORTG_BitMapInfo(s->RastPort.BitMap, &bi) || bi.depth != 8) {
        printf("OpenRTGLineCheck: no 8-bit GFOpenRTG screen (mode %08lx)\n", (unsigned long)id);
        if (s) CloseScreen(s);
        return 20;
    }
    for (i = 0; i < 256; i++) SetRGB32(&s->ViewPort, i, (ULONG)(i * 0x9E3779B1UL), (ULONG)(i * 0x7F4A7C15UL), (ULONG)(i * 0x2545F491UL));
    SetRGB32(&s->ViewPort, 0, 0x30303030, 0x30303030, 0x40404040);
    SetRGB32(&s->ViewPort, 1, 0xD0D0D0D0, 0xD0D0D0D0, 0xD0D0D0D0);
    SetRGB32(&s->ViewPort, 2, 0xFFFFFFFF, 0x40404040, 0x20202020);
    SetRGB32(&s->ViewPort, 3, 0x30303030, 0x80808080, 0xF0F0F0F0);
    SetRGB32(&s->ViewPort, 254, 0x2F2F2F2F, 0x2F2F2F2F, 0x3F3F3F3F);  /* ~1 on the darker grey */
    SetRGB32(&s->ViewPort, 253, 0x0F0F0F0F, 0xBFBFBFBF, 0xDFDFDFDF);  /* ~2 */
    SetRGB32(&s->ViewPort, 252, 0xCFCFCFCF, 0x7F7F7F7F, 0x0F0F0F0F);  /* ~3 */
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, 20, WA_Top, 600 - H - 20, WA_Width, W, WA_Height, H, WA_Borderless, TRUE,
                         WA_SmartRefresh, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE, TAG_DONE);
    cover = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, 20 + 60, WA_Top, 600 - H - 20 + 30, WA_Width, 50, WA_Height, 40,
                           WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, TAG_DONE);
    ref_bm = AllocBitMap(W, H, 8, BMF_CLEAR, NULL);
    rtg_bm = AllocBitMap(W, H, 8, BMF_CLEAR, s->RastPort.BitMap);
    if (!win || !cover || !ref_bm || !rtg_bm || ORTG_BitMapInfo(ref_bm, &bi) || !ORTG_BitMapInfo(rtg_bm, &bi)) {
        printf("OpenRTGLineCheck: no windows or bitmaps (the planar one must be planar, the other GFOpenRTG's)\n");
        return 20;
    }
    InitRastPort(&ref_rp); ref_rp.BitMap = ref_bm;
    InitRastPort(&bmp_rp); bmp_rp.BitMap = rtg_bm;
    rps[REF] = &ref_rp; rps[WIN] = win->RPort; rps[BMP] = &bmp_rp;
    if (do_check) {
        ORTG_DrawStats(NULL, 0, ORTG_DRAW_OPENGPU);
        check(rounds, "GFOpenGPU");
        ORTG_DrawStats(NULL, 0, ORTG_DRAW_CPU_ONLY);
        check(rounds, "CPU only");
        ORTG_DrawStats(NULL, 0, ORTG_DRAW_OPENGPU);
        printf("CHECK %d checks: %s\n", checks, fails ? "DIFFERENT" : "all the same as planar");
    }
    if (do_time) {
        struct Window *big = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)s, WA_Left, 200, WA_Top, 300, WA_Width, 360, WA_Height, 220,
                                            WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, TAG_DONE);
        if (big) {
            printf("TIME window 360x220, 8-bit, GFOpenGPU on:\n");
            ORTG_DrawStats(NULL, 0, ORTG_DRAW_OPENGPU);
            timing(big->RPort, 360, 220);
            printf("TIME the same, ORTG_DRAW_CPU_ONLY:\n");
            ORTG_DrawStats(NULL, 0, ORTG_DRAW_CPU_ONLY);
            timing(big->RPort, 360, 220);
            ORTG_DrawStats(NULL, 0, ORTG_DRAW_OPENGPU);
            CloseWindow(big);
        }
    }
    if (do_show) {
        CloseWindow(cover); cover = NULL;
        CloseWindow(win); win = NULL;
        show(&s->RastPort, &ref_rp, secs);
    }
    if (cover) CloseWindow(cover);
    if (win) CloseWindow(win);
    FreeBitMap(ref_bm); FreeBitMap(rtg_bm);
    CloseScreen(s);
    CloseDevice(&treq.tr_node);
    CloseLibrary(OpenRTGBase);
    return fails ? 10 : 0;
}
