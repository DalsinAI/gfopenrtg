/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * RetainedTest: gfopengpu.library 0.25's retained surfaces on an Amiga.
 * For an 8-bit (INDEX8, with a palette) and a 32-bit surface, on the GPU
 * path (when there is a driver) and on the software path (OGPU_RSF_SOFTWARE):
 *   - create a 200 x 150 surface (7 x 5 = 35 tiles), draw into it, present:
 *     35 tiles; present again: 0; change one pixel: 1; mark a rectangle: the
 *     tiles it touches; on 8-bit, change the palette: 0 tiles on the GPU path;
 *   - the screen's pixels against the surface's colours after each present
 *     (read here with cybergraphics.library's ReadPixelArray: the library
 *     never reads the screen), 1:1, and scaled 2x on the GPU path;
 *   - the same in a window (its layer's ClipRects);
 *   - OGPU_RetainedStats: frames, tiles, readbacks 0;
 *   - the cost of a present with nothing changed (hashing) and with one
 *     tile, for a 640 x 480 surface.
 * On a 32-bit GFOpenRTG screen of its own (or the Workbench screen when it
 * is one). Prints PASS and FAIL lines and a summary; returns 0, 10 when a
 * check failed, 20 when it couldn't run.
 *   RetainedTest */
#include <exec/types.h>
#include <exec/memory.h>
#include <devices/timer.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/cybergraphics.h>
#include <proto/timer.h>
#include <proto/gfopengpu.h>
#include <stdio.h>
#include <string.h>

struct Library *OpenGPUBase, *CyberGfxBase;
struct Device *TimerBase;
static struct timerequest treq;
static int checks, fails;
static ULONG *grab_buf;

#define W 200
#define H 150
#define TILES 35

#define CHECK(cond, ...) do { checks++; if (cond) { printf("PASS "); } else { fails++; printf("FAIL "); } printf(__VA_ARGS__); printf("\n"); } while (0)

static double now_ms(void)
{
    struct EClockVal e;
    ULONG hz = ReadEClock(&e);
    return ((double)e.ev_hi * 4294967296.0 + (double)e.ev_lo) * 1000.0 / (double)hz;
}

static ULONG seed = 7;
static ULONG rnd(void) { seed = seed * 1103515245UL + 12345UL; return seed >> 8; }

static ULONG colour_of(struct OGPURetained *rs, int x, int y)
{
    const UBYTE *row = (const UBYTE *)rs->rs_Pixels + (ULONG)y * rs->rs_BytesPerRow;
    if (rs->rs_Format == OGPU_FMT_INDEX8) return rs->rs_Palette[row[x]] & 0xFFFFFFUL;
    return ((const ULONG *)row)[x] & 0xFFFFFFUL;
}

static void put(struct OGPURetained *rs, int x, int y, ULONG v)
{
    UBYTE *row = (UBYTE *)rs->rs_Pixels + (ULONG)y * rs->rs_BytesPerRow;
    if (rs->rs_Format == OGPU_FMT_INDEX8) row[x] = (UBYTE)v;
    else ((ULONG *)row)[x] = v & 0xFFFFFFUL;
}

static void draw(struct OGPURetained *rs)
{
    int x, y;
    for (y = 0; y < rs->rs_Height; y++)
        for (x = 0; x < rs->rs_Width; x++)
            put(rs, x, y, rs->rs_Format == OGPU_FMT_INDEX8 ? (ULONG)((x ^ y) + (x >> 3)) & 255 : (ULONG)x * 0x10305UL ^ (ULONG)y * 0x30501UL);
}

/* The screen at (sx, sy) against the surface, scaled by k (each screen
 * pixel the surface's at its integer position): the wrong pixels. */
static int compare(struct RastPort *rp, struct OGPURetained *rs, int sx, int sy, int k)
{
    int w = rs->rs_Width * k, h = rs->rs_Height * k, x, y, bad = 0;
    ReadPixelArray(grab_buf, 0, 0, (UWORD)(w * 4), rp, (UWORD)sx, (UWORD)sy, (UWORD)w, (UWORD)h, RECTFMT_ARGB);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            ULONG got = grab_buf[y * w + x] & 0xFFFFFFUL, want = colour_of(rs, x / k, y / k);
            if (got != want && bad++ < 3) printf("    at %d,%d: %06lx, not %06lx\n", sx + x, sy + y, (unsigned long)got, (unsigned long)want);
        }
    return bad;
}

static void stats_line(struct OGPURetained *rs, const char *what)
{
    struct OGPURetainedStats st;
    memset(&st, 0, sizeof st);
    st.size = sizeof st;
    OGPU_RetainedStats(rs, &st);
    printf("  stats %s: frames %lu, tiles uploaded %lu, readbacks %lu, tiles %lu, hashed %lu, palette uploads %lu, gpu frames %lu, cpu frames %lu\n",
           what, (unsigned long)st.frames, (unsigned long)st.tiles_uploaded, (unsigned long)st.readbacks, (unsigned long)st.tiles,
           (unsigned long)st.tiles_hashed, (unsigned long)st.palette_uploads, (unsigned long)st.gpu_frames, (unsigned long)st.cpu_frames);
    CHECK(st.readbacks == 0 && st.tiles == TILES, "%s: readbacks 0, %d tiles", what, TILES);
}

/* One surface through its sequence on rp at x, y. software: OGPU_RSF_SOFTWARE. */
static void sequence(struct RastPort *rp, int x, int y, ULONG format, int software, const char *where)
{
    char what[80];
    struct OGPURetained *rs;
    struct OGPURetainedStats st;
    LONG n;
    int gpu;
    snprintf(what, sizeof what, "%s %s %s", where, format == OGPU_FMT_INDEX8 ? "INDEX8" : "XRGB32", software ? "software" : "default");
    rs = OGPU_RetainedCreate(W, H, format, software ? OGPU_RSF_SOFTWARE : 0);
    CHECK(rs != NULL, "%s: created (%lu bytes a row)", what, rs ? (unsigned long)rs->rs_BytesPerRow : 0UL);
    if (!rs) return;
    if (format == OGPU_FMT_INDEX8) {
        ULONG pal[256];
        int i;
        for (i = 0; i < 256; i++) pal[i] = 0xFF000000UL | (ULONG)i << 16 | (ULONG)(255 - i) << 8 | (ULONG)((i * 7) & 255);
        OGPU_RetainedPalette(rs, 0, 256, pal);
    }
    draw(rs);
    n = OGPU_RetainedPresent(rs, rp, x, y, 0, 0);
    memset(&st, 0, sizeof st);
    st.size = sizeof st;
    OGPU_RetainedStats(rs, &st);
    gpu = st.gpu_frames == 1;
    printf("  %s: the %s path\n", what, gpu ? "GPU" : "software");
    if (software) CHECK(!gpu, "%s: OGPU_RSF_SOFTWARE takes the software path", what);
    CHECK(n == TILES, "%s: first present uploads %d tiles (%ld)", what, TILES, (long)n);
    CHECK(compare(rp, rs, x, y, 1) == 0, "%s: first present on the screen", what);
    n = OGPU_RetainedPresent(rs, rp, x, y, 0, 0);
    CHECK(n == 0, "%s: second present uploads 0 tiles (%ld)", what, (long)n);
    CHECK(compare(rp, rs, x, y, 1) == 0, "%s: second present on the screen", what);
    put(rs, 100, 70, format == OGPU_FMT_INDEX8 ? 3 : 0x123456);
    n = OGPU_RetainedPresent(rs, rp, x, y, 0, 0);
    CHECK(n == 1, "%s: one pixel changed: 1 tile (%ld)", what, (long)n);
    CHECK(compare(rp, rs, x, y, 1) == 0, "%s: the changed pixel on the screen", what);
    put(rs, 5, 5, format == OGPU_FMT_INDEX8 ? 9 : 0x654321);
    put(rs, 40, 40, format == OGPU_FMT_INDEX8 ? 10 : 0x0F0F0F);
    OGPU_RetainedMark(rs, 0, 0, 10, 10);
    OGPU_RetainedMark(rs, 35, 35, 10, 10);
    n = OGPU_RetainedPresent(rs, rp, x, y, 0, 0);
    CHECK(n == 2, "%s: two marked rectangles: 2 tiles (%ld)", what, (long)n);
    CHECK(compare(rp, rs, x, y, 1) == 0, "%s: marked tiles on the screen", what);
    if (format == OGPU_FMT_INDEX8) {
        ULONG c = 0xFF00FF00UL;
        OGPU_RetainedPalette(rs, 3, 1, &c);
        n = OGPU_RetainedPresent(rs, rp, x, y, 0, 0);
        /* the GPU path uploads the palette alone; the software path rewrites every tile through it
         * (after the marked present, the hashing one first uploads the 2 marked tiles once more) */
        if (gpu) CHECK(n == 2, "%s: palette change: no tile but the 2 marked before (%ld)", what, (long)n);
        else CHECK(n == TILES, "%s: palette change, software: every tile (%ld)", what, (long)n);
        CHECK(compare(rp, rs, x, y, 1) == 0, "%s: the new palette on the screen", what);
        n = OGPU_RetainedPresent(rs, rp, x, y, 0, 0);
        OGPU_RetainedPalette(rs, 3, 1, &c);
        n = OGPU_RetainedPresent(rs, rp, x, y, 0, 0);
        if (gpu) CHECK(n == 0, "%s: palette set again: 0 tiles (%ld)", what, (long)n);
    } else {
        n = OGPU_RetainedPresent(rs, rp, x, y, 0, 0);
        CHECK(n == 2, "%s: hashing after marking: the 2 marked tiles once more (%ld)", what, (long)n);
    }
    n = OGPU_RetainedPresent(rs, rp, x, y, 0, 0);
    CHECK(n == 0, "%s: settled: 0 tiles (%ld)", what, (long)n);
    if (gpu) {
        n = OGPU_RetainedPresent(rs, rp, x, y, W * 2, H * 2);
        CHECK(n == 0, "%s: scaled 2x: 0 tiles (%ld)", what, (long)n);
        CHECK(compare(rp, rs, x, y, 2) == 0, "%s: scaled 2x on the screen", what);
    }
    stats_line(rs, what);
    OGPU_RetainedDestroy(rs);
}

static void timing(struct RastPort *rp, ULONG format)
{
    struct OGPURetained *rs = OGPU_RetainedCreate(640, 480, format, 0);
    struct OGPURetainedStats st;
    double t0, t1, t2;
    int i;
    LONG n1 = 0, n2 = 0;
    if (!rs) { printf("  timing: no surface\n"); return; }
    draw(rs);
    OGPU_RetainedPresent(rs, rp, 0, 0, 0, 0);
    t0 = now_ms();
    for (i = 0; i < 10; i++) n1 += OGPU_RetainedPresent(rs, rp, 0, 0, 0, 0);
    t1 = now_ms();
    for (i = 0; i < 10; i++) {
        put(rs, (int)(rnd() % 640), (int)(rnd() % 480), rnd());
        n2 += OGPU_RetainedPresent(rs, rp, 0, 0, 0, 0);
    }
    t2 = now_ms();
    memset(&st, 0, sizeof st);
    st.size = sizeof st;
    OGPU_RetainedStats(rs, &st);
    printf("  time 640x480 %s (%s path): unchanged %.2f ms a present (%ld tiles in 10), one pixel %.2f ms (%ld tiles in 10)\n",
           format == OGPU_FMT_INDEX8 ? "INDEX8" : "XRGB32", st.gpu_frames ? "GPU" : "software", (t1 - t0) / 10, (long)n1, (t2 - t1) / 10, (long)n2);
    CHECK(n1 == 0 && n2 == 10, "640x480 %s: 0 tiles unchanged, 1 a changed pixel", format == OGPU_FMT_INDEX8 ? "INDEX8" : "XRGB32");
    OGPU_RetainedDestroy(rs);
}

int main(void)
{
    struct Screen *scr = NULL, *wb = NULL, *on;
    struct Window *win = NULL;
    ULONG id = INVALID_ID;
    int rc = 0;
    if (!(OpenGPUBase = OpenLibrary((CONST_STRPTR)"gfopengpu.library", 0))) { printf("RetainedTest: no gfopengpu.library\n"); return 20; }
    printf("RetainedTest: gfopengpu.library %d.%d, back end 1: %s\n", OpenGPUBase->lib_Version, OpenGPUBase->lib_Revision,
           OGPU_BackEndName(1) ? (char *)OGPU_BackEndName(1) : "(none: the CPU only)");
    if (OpenGPUBase->lib_Version == 0 && OpenGPUBase->lib_Revision < OGPU_RETAINED_REVISION) {
        printf("RetainedTest: needs gfopengpu.library 0.%d\n", OGPU_RETAINED_REVISION);
        CloseLibrary(OpenGPUBase);
        return 20;
    }
    if (!(CyberGfxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 41))) { printf("RetainedTest: no cybergraphics.library\n"); rc = 20; goto out; }
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, &treq.tr_node, 0)) { printf("RetainedTest: no timer.device\n"); rc = 20; goto out; }
    TimerBase = treq.tr_node.io_Device;
    if (!(grab_buf = AllocVec(W * 2 * H * 2 * 4, MEMF_ANY))) { rc = 20; goto out; }
    {
        struct TagItem bt[] = { { CYBRBIDTG_Depth, 32 }, { CYBRBIDTG_NominalWidth, 640 }, { CYBRBIDTG_NominalHeight, 480 }, { TAG_DONE, 0 } };
        id = BestCModeIDTagList(bt);
    }
    if (id != INVALID_ID)
        scr = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, 640, SA_Height, 480, SA_Depth, 32, SA_Title, (ULONG)"RetainedTest",
                             SA_ShowTitle, FALSE, SA_Quiet, TRUE, TAG_DONE);
    if (scr && GetCyberMapAttr(scr->RastPort.BitMap, CYBRMATTR_DEPTH) < 24) { CloseScreen(scr); scr = NULL; }
    if (!scr) {
        wb = LockPubScreen(NULL);
        if (!wb || !GetCyberMapAttr(wb->RastPort.BitMap, CYBRMATTR_ISCYBERGFX) || GetCyberMapAttr(wb->RastPort.BitMap, CYBRMATTR_DEPTH) < 24) {
            printf("RetainedTest: no 32-bit screen of its own, and the Workbench screen isn't one\n");
            rc = 20;
            goto out;
        }
        printf("RetainedTest: on the Workbench screen\n");
    }
    on = scr ? scr : wb;
    printf("RetainedTest: screen %dx%d, pixel format %lu\n", on->Width, on->Height,
           (unsigned long)GetCyberMapAttr(on->RastPort.BitMap, CYBRMATTR_PIXFMT));
    if (scr) {
        sequence(&scr->RastPort, 20, 30, OGPU_FMT_INDEX8, 0, "screen");
        sequence(&scr->RastPort, 20, 30, OGPU_FMT_XRGB32, 0, "screen");
        sequence(&scr->RastPort, 20, 30, OGPU_FMT_INDEX8, 1, "screen");
        sequence(&scr->RastPort, 20, 30, OGPU_FMT_XRGB32, 1, "screen");
    }
    win = OpenWindowTags(NULL, scr ? WA_CustomScreen : WA_PubScreen, (ULONG)on, WA_Left, 220, WA_Top, 20,
                         WA_InnerWidth, W * 2 + 10, WA_InnerHeight, H * 2 + 10, WA_Title, (ULONG)"RetainedTest",
                         WA_SimpleRefresh, TRUE, WA_Activate, FALSE, WA_DragBar, TRUE, WA_DepthGadget, TRUE, TAG_DONE);
    if (win) {
        sequence(win->RPort, win->BorderLeft + 4, win->BorderTop + 4, OGPU_FMT_INDEX8, 0, "window");
        sequence(win->RPort, win->BorderLeft + 4, win->BorderTop + 4, OGPU_FMT_XRGB32, 0, "window");
        sequence(win->RPort, win->BorderLeft + 4, win->BorderTop + 4, OGPU_FMT_XRGB32, 1, "window");
    } else {
        printf("RetainedTest: no window\n");
    }
    if (win) { CloseWindow(win); win = NULL; }
    if (scr) {
        timing(&scr->RastPort, OGPU_FMT_INDEX8);
        timing(&scr->RastPort, OGPU_FMT_XRGB32);
    }
    printf("RetainedTest: %d checks, %d failed\n", checks, fails);
    if (fails) rc = 10;
out:
    if (win) CloseWindow(win);
    if (scr) CloseScreen(scr);
    if (wb) UnlockPubScreen(NULL, wb);
    if (grab_buf) FreeVec(grab_buf);
    if (TimerBase) CloseDevice(&treq.tr_node);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    CloseLibrary(OpenGPUBase);
    return rc;
}
